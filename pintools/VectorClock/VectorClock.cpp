/*! @file
 *  This is a race condition detector using vector clocks.
 */

#include "pin.H"
#include <cxxabi.h>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>

#include "Elf.hpp"

using namespace std;

/* ================================================================== */
// Global variables 
/* ================================================================== */

std::ostream * out = &cerr;

PIN_LOCK lock;
PIN_LOCK vc_lock;

template <class T>
class VC {
 public:
  VC() : clocks_{} {}
  VC(THREADID tid, T value) : clocks_{} {
    clocks_[tid] = value;
  }

  T& operator [](THREADID tid) {
    return clocks_[tid];
  }
  map<THREADID, T>::iterator find(THREADID tid) {
    return clocks_.find(tid);
  }
  map<THREADID, T>::const_iterator find(THREADID tid) const {
    return clocks_.find(tid);
  }

  VC& operator |=(const VC<T>& rhs) {
    for (auto [tid, v] : rhs.clocks_) {
      if (clocks_[tid] < v) {
        clocks_[tid] = v;
      }
    }
    return *this;
  }

  bool operator <=(const VC<T>& rhs) const {
    for (auto [tid, v] : clocks_) {
      auto it = rhs.clocks_.find(tid);
      if (it == rhs.clocks_.end() && v > 0) {
        return false;
      } else if (v > it->second) {
        return false;
      }
    }
    return true;
  }

  bool operator >(const VC<T>& rhs) const {
    return !(*this <= rhs);
  }

  auto begin() const noexcept {
    return clocks_.begin();
  }
  auto end() const noexcept {
    return clocks_.end();
  }

 private:
  map<THREADID, T> clocks_;
};

template <class T>
ostream& operator <<(ostream& os, const VC<T>& vc) {
  char sep = '<';
  for (auto [k, v] : vc) {
    os << sep << 'T' << k << ':' << v;
    sep = ',';
  }
  os << '>';
  return os;
}

template <class T>
class ThreadVCMap {
 public:
  VC<T>& operator [](THREADID tid) {
    if (m_.count(tid) == 0) {
      m_[tid][tid] = 1;
    }
    return m_[tid];
  }

  auto begin() const noexcept {
    return m_.begin();
  }
  auto end() const noexcept {
    return m_.end();
  }

 private:
  map<THREADID, VC<T>> m_;
};

ThreadVCMap<int> thread_vc;
map<ADDRINT, VC<int>> read_vc, write_vc, lock_vc;

/* ===================================================================== */
// Command line switches
/* ===================================================================== */
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE,  "pintool",
    "o", "", "specify file name for MyPinTool output");

/* ===================================================================== */
// Utilities
/* ===================================================================== */

/*!
 *  Print out help message.
 */
INT32 Usage() {
  cerr << "This tool prints out the number of dynamically executed " << endl <<
    "instructions, basic blocks and threads in the application." << endl << endl;

  cerr << KNOB_BASE::StringKnobSummary() << endl;

  return -1;
}

string Demangle(const char* symbol) {
  int status;
  char* demangled = abi::__cxa_demangle(symbol, nullptr, nullptr, &status);
  if (status) {
    return symbol;
  }

  string s{demangled};
  free(demangled);
  return s;
}

class LockGuard {
 public:
  LockGuard(PIN_LOCK& l) : l_{l} {
    PIN_GetLock(&l_, PIN_ThreadId());
  }

  ~LockGuard() {
    PIN_ReleaseLock(&l_);
  }

 private:
  PIN_LOCK& l_;
};

/*!
 * Load symbol addresses from the target binary
 * into read_vc, write_vc, and lock_vc.
 * @param[in]  argc  the 1st argument of main()
 * @param[in]  argv  the 2nd argument of main()
 * @param[in]  watch_vars  variable names to be watched by this pintool
 * @param[in]  watch_locks  lock names to be watched by this pintool
 */
bool LoadSymbolAddrFromTargetBinary(
    int argc, char** argv,
    const set<string>& watch_vars, const set<string>& watch_locks) {

  const char* target_bin_path = nullptr;
  for (int i = argc - 2; i > 0; --i) {
    if (strcmp(argv[i], "--") == 0) {
      target_bin_path = argv[i + 1];
      break;
    }
  }
  if (!target_bin_path) {
    return true;
  }

  map<string, Elf64_Sym> syms;
  if (GetSymbols(target_bin_path, syms)) {
    return true;
  }

  for (const auto& [name, sym] : syms) {
    if (ELF64_ST_TYPE(sym.st_info) != STT_OBJECT) {
      continue;
    }

    const auto addr = sym.st_value;
    if (watch_vars.count(name)) {
      read_vc[addr] = VC<int>{};
      write_vc[addr] = VC<int>{};
    } else if (watch_locks.count(name)) {
      lock_vc[addr] = VC<int>{};
    }
  }

  return false;
}

/* ===================================================================== */
// Analysis routines
/* ===================================================================== */

void Read(THREADID tid, ADDRINT mem_addr) {
  LockGuard l{vc_lock};
  read_vc[mem_addr][tid] = thread_vc[tid][tid];
}

void Write(THREADID tid, ADDRINT mem_addr) {
  LockGuard l{vc_lock};
  write_vc[mem_addr][tid] = thread_vc[tid][tid];
}

void Aquire(THREADID tid, ADDRINT lock_addr) {
  LockGuard l{vc_lock};
  thread_vc[tid] |= lock_vc[lock_addr];
}

void Release(THREADID tid, ADDRINT lock_addr) {
  LockGuard l{vc_lock};
  lock_vc[lock_addr] = thread_vc[tid];
  ++thread_vc[tid][tid];
}

bool NoRaceForWrite(THREADID tid, ADDRINT mem_addr) {
  return read_vc[mem_addr] <= thread_vc[tid] &&
         write_vc[mem_addr] <= thread_vc[tid];
}

bool NoRaceForRead(THREADID tid, ADDRINT mem_addr) {
  return write_vc[mem_addr] <= thread_vc[tid];
}

map<void*, THREADID> thread_to_id;

void Fork(THREADID tid, void* thread_obj) {
  static THREADID last_id = 0;

  LockGuard l{vc_lock};
  ++last_id;
  thread_to_id[thread_obj] = last_id;

  thread_vc[last_id] |= thread_vc[tid];
  ++thread_vc[tid][tid];
}

void Join(THREADID tid, void* thread_obj) {
  LockGuard l{vc_lock};
  const auto join_id = thread_to_id[thread_obj];
  thread_vc[tid] |= thread_vc[join_id];
  ++thread_vc[join_id][join_id];
}

/*!
 * CheckOverflow detects out-of-bounds memory access.
 * An access is out-of-bounds if mem_addr doesn't match any of heap objects.
 * @param[in]  ins_addr  address of the memory-access instruction
 * @param[in]  mem_addr  effective address of the memory operand
 * @param[in]  is_write  true if the memory operand is written
 */
void CheckOverflow(ADDRINT ins_addr, ADDRINT mem_addr, BOOL is_write) {
  if (read_vc.count(mem_addr) == 0) {
    return;
  }

  const auto tid = PIN_ThreadId();
  PIN_GetLock(&lock, tid);

  //if (thread_vc[tid][tid] == 0) {
  //  thread_vc[tid][tid] = 1;
  //}

  if (is_write) {
    Write(tid, mem_addr);
    //write_vc[mem_addr][tid] = thread_vc[tid][tid];
    if (!NoRaceForWrite(tid, mem_addr)) {
      *out << "Write race: C[" << tid << "]=" << thread_vc[tid]
           << ", R[" << mem_addr << "]=" << read_vc[mem_addr]
           << ", W[" << mem_addr << "]=" << write_vc[mem_addr]
           << endl;
    }
  } else {
    Read(tid, mem_addr);
    //read_vc[mem_addr][tid] = thread_vc[tid][tid];
    if (!NoRaceForRead(tid, mem_addr)) {
      *out << "Read race: C[" << tid << "]=" << thread_vc[tid]
           << ", W[" << mem_addr << "]=" << write_vc[mem_addr]
           << endl;
    }
  }

  const char* type = is_write ? "write" : "read";
  *out << hex << "Found " << type << " variable 'x'"
       << " by thread " << PIN_ThreadId()
       << " at 0x" << mem_addr << " (IP=0x" << ins_addr << ")" << endl;
  PIN_ReleaseLock(&lock);
}

bool main_started = false;

void OnMainStarted() {
  main_started = true;
}

/*!
 * MutexLockWrapper calls mutex.lock() and process vector clocks.
 * @param[in]  ctx  
 */
void MutexLockWrapper(CONTEXT* ctx, AFUNPTR orig_func_ptr, void* m) {
  const auto tid = PIN_ThreadId();

  PIN_CallApplicationFunction(ctx, tid, CALLINGSTD_DEFAULT,
                              orig_func_ptr, nullptr,
                              PIN_PARG(void),
                              PIN_PARG(void*), m,
                              PIN_PARG_END());
  // PIN_PARG(void) must appear first in the argument list
  // when the function has no return value.

  const ADDRINT mtx_addr = reinterpret_cast<ADDRINT>(m);
  if (lock_vc.count(mtx_addr)) {
    Aquire(tid, mtx_addr);
  }
}

/*!
 * MutexUnlockWrapper calls mutex.unlock() and process vector clocks.
 * @param[in]  ctx  
 */
void MutexUnlockWrapper(CONTEXT* ctx, AFUNPTR orig_func_ptr, void* m) {
  const auto tid = PIN_ThreadId();

  const ADDRINT mtx_addr = reinterpret_cast<ADDRINT>(m);
  if (lock_vc.count(mtx_addr)) {
    Release(tid, mtx_addr);
  }

  PIN_CallApplicationFunction(ctx, tid, CALLINGSTD_DEFAULT,
                              orig_func_ptr, nullptr,
                              PIN_PARG(void),
                              PIN_PARG(void*), m,
                              PIN_PARG_END());
}

void ThreadCtorWrapper(CONTEXT* ctx, AFUNPTR orig_func_ptr, void* t) {
  const auto tid = PIN_ThreadId();

  cout << "thread ctor by thread " << tid << endl;

  Fork(tid, t);

  PIN_CallApplicationFunction(ctx, tid, CALLINGSTD_DEFAULT,
                              orig_func_ptr, nullptr,
                              PIN_PARG(void),
                              PIN_PARG(void*), t,
                              PIN_PARG_END());
}

void ThreadJoinWrapper(CONTEXT* ctx, AFUNPTR orig_func_ptr, void* t) {
  const auto tid = PIN_ThreadId();

  PIN_CallApplicationFunction(ctx, tid, CALLINGSTD_DEFAULT,
                              orig_func_ptr, nullptr,
                              PIN_PARG(void),
                              PIN_PARG(void*), t,
                              PIN_PARG_END());

  cout << "thread::join by thread " << tid << endl;

  Join(tid, t);
}

/* ===================================================================== */
// Instrumentation callbacks
/* ===================================================================== */

// id of the main() routine set by InsertMainMarker().
UINT32 main_rtn_id;

/*!
 * ObserveMemAccess inserts call to the CheckOverflow() analysis routine
 * before every memory-accessing instructions inside main().
 * @param[in]  trace  trace to be instrumented
 */
VOID ObserveMemAccess(TRACE trace, VOID*) {
  //RTN rtn = TRACE_Rtn(trace);
  //if (!RTN_Valid(rtn) || RTN_Id(rtn) != main_rtn_id) {
  //  return;
  //}

  for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
    for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins)) {
      REG base_reg = INS_MemoryBaseReg(ins);
      if (base_reg == REG_RSP || base_reg == REG_RBP || base_reg == REG_RIP) {
        continue;
      }

      for (UINT32 memop = 0; memop < INS_MemoryOperandCount(ins); ++memop) {
        if (!INS_MemoryOperandIsRead(ins, memop) &&
            !INS_MemoryOperandIsWritten(ins, memop)) {
          continue;
        }

        INS_InsertCall(
            ins, IPOINT_BEFORE, reinterpret_cast<AFUNPTR>(CheckOverflow),
            IARG_INST_PTR,
            IARG_MEMORYOP_EA, memop,
            IARG_BOOL, INS_MemoryOperandIsWritten(ins, memop),
            IARG_END);
      }
    }
  }
}

/*!
 * ReplaceLock replaces lock()/unlock() of std::mutex with wrapper functions.
 * @param[in]  img  image to be instrumented
 */
VOID ReplaceLock(IMG img, VOID*) {
  RTN lock_rtn = RTN_FindByName(img, "_ZNSt5mutex4lockEv");
  RTN unlock_rtn = RTN_FindByName(img, "_ZNSt5mutex6unlockEv");

  if (RTN_Valid(lock_rtn)) {
    cout << "found std::mutex::lock" << endl;
    RTN_ReplaceSignature(
        lock_rtn, reinterpret_cast<AFUNPTR>(MutexLockWrapper),
        IARG_CONTEXT,
        IARG_ORIG_FUNCPTR,
        IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
        IARG_END);
  }

  if (RTN_Valid(unlock_rtn)) {
    cout << "found std::mutex::unlock" << endl;
    RTN_ReplaceSignature(
        unlock_rtn, reinterpret_cast<AFUNPTR>(MutexUnlockWrapper),
        IARG_CONTEXT,
        IARG_ORIG_FUNCPTR,
        IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
        IARG_END);
  }
}

VOID ReplaceThread(IMG img, VOID*) {
  RTN ctor_rtn = RTN_FindByName(img, "_ZNSt6threadC1IRFvvEJEvEEOT_DpOT0_");
  RTN join_rtn = RTN_FindByName(img, "_ZNSt6thread4joinEv");

  if (RTN_Valid(ctor_rtn)) {
    cout << "found std::thread::thread" << endl;
    RTN_ReplaceSignature(
        ctor_rtn, reinterpret_cast<AFUNPTR>(ThreadCtorWrapper),
        IARG_CONTEXT,
        IARG_ORIG_FUNCPTR,
        IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
        IARG_END);
  }

  if (RTN_Valid(join_rtn)) {
    cout << "found std::thread::join" << endl;
    RTN_ReplaceSignature(
        join_rtn, reinterpret_cast<AFUNPTR>(ThreadJoinWrapper),
        IARG_CONTEXT,
        IARG_ORIG_FUNCPTR,
        IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
        IARG_END);
  }
}

/*!
 * InsertMainMarker inserts OnMainStarted() just before main().
 * @param[in]  img  image to be instrumented.
 */
VOID InsertMainMarker(IMG img, VOID*) {
  RTN main_rtn = RTN_FindByName(img, "main");
  if (RTN_Valid(main_rtn)) {
    RTN_Open(main_rtn);
    RTN_InsertCall(main_rtn, IPOINT_BEFORE,
        reinterpret_cast<AFUNPTR>(OnMainStarted), IARG_END);
    RTN_Close(main_rtn);

    main_rtn_id = RTN_Id(main_rtn);
  }
}

/*!
 * Print out analysis results.
 * This function is called when the application exits.
 * @param[in]   code            exit code of the application
 * @param[in]   v               value specified by the tool in the 
 *                              PIN_AddFiniFunction function call
 */
VOID Fini(INT32 code, VOID* v) {
  PIN_GetLock(&lock, PIN_ThreadId());

  *out << "===============================================" << endl;
  vector<THREADID> tids;
  for (const auto& [tid, vc] : thread_vc) {
    tids.push_back(tid);
  }
  for (THREADID tid : tids) {
    *out << "Thread " << tid << "'s VC: " << thread_vc[tid];
  }

  vector<ADDRINT> locs;
  for (const auto& [loc, vc] : read_vc) {
    locs.push_back(loc);
  }
  for (ADDRINT loc : locs) {
    *out << "Read VC for location " << hex << loc
         << ": " << read_vc[loc] << endl;
  }
  for (ADDRINT loc : locs) {
    *out << "Write VC for location " << hex << loc
         << ": <" << write_vc[loc] << endl;
  }
  *out << "===============================================" << endl;

  PIN_ReleaseLock(&lock);
}

/*!
 * The main procedure of the tool.
 * This function is called when the application image is loaded but not yet started.
 * @param[in]   argc            total number of elements in the argv array
 * @param[in]   argv            array of command line arguments, 
 *                              including pin -t <toolname> -- ...
 */
int main(int argc, char** argv) {
  PIN_InitSymbols();

  if (PIN_Init(argc, argv)) {
    return Usage();
  }

  set<string> watch_vars, watch_locks;
  watch_vars.insert("x");
  watch_locks.insert("m");

  if (LoadSymbolAddrFromTargetBinary(
      argc, argv, watch_vars, watch_locks)) {
    return Usage();
  }

  if (!KnobOutputFile.Value().empty()) {
    out = new std::ofstream(KnobOutputFile.Value().c_str());
  }

  IMG_AddInstrumentFunction(ReplaceLock, 0);
  IMG_AddInstrumentFunction(InsertMainMarker, 0);
  IMG_AddInstrumentFunction(ReplaceThread, 0);
  TRACE_AddInstrumentFunction(ObserveMemAccess, 0);
  PIN_AddFiniFunction(Fini, 0);

  cerr << "===============================================" << endl;
  cerr << "This application is instrumented by Overflow" << endl;
  if (!KnobOutputFile.Value().empty()) {
    cerr << "See file " << KnobOutputFile.Value()
         << " for analysis results" << endl;
  }
  cerr << "===============================================" << endl;

  PIN_InitLock(&lock);
  PIN_InitLock(&vc_lock);

  // Start the program, never returns
  PIN_StartProgram();

  return 0;
}
