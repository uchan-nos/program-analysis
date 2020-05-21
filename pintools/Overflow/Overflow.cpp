
/*! @file
 *  This is an example of the PIN tool that demonstrates some basic PIN APIs 
 *  and could serve as the starting point for developing your first PIN tool
 */

#include "pin.H"
#include <iostream>
#include <fstream>

using namespace std;

/* ================================================================== */
// Global variables 
/* ================================================================== */

std::ostream * out = &cerr;

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

/* ===================================================================== */
// Analysis routines
/* ===================================================================== */

struct HeapObject {
  ADDRINT addr;
  size_t size;
};

/*!
 * heap_objs records objects allocated by malloc().
 */
vector<HeapObject> heap_objs;

/*!
 * CheckOverflow detects out-of-bounds memory access.
 * An access is out-of-bounds if mem_addr doesn't match any of heap objects.
 * @param[in]  ins_addr  address of the memory-access instruction
 * @param[in]  mem_addr  effective address of the memory operand
 * @param[in]  is_write  true if the memory operand is written
 */
void CheckOverflow(ADDRINT ins_addr, ADDRINT mem_addr, BOOL is_write) {
  bool out_of_bound = true;
  for (auto& heap_obj : heap_objs) {
    if (heap_obj.addr <= mem_addr && mem_addr < heap_obj.addr + heap_obj.size) {
      out_of_bound = false;
      break;
    }
  }
  if (out_of_bound) {
    const char* type = is_write ? "write" : "read";
    *out << hex << "Found out-of-bounds memory " << type
         << " at 0x" << mem_addr << " (IP=0x" << ins_addr << ")" << endl;
  }
}

bool main_started = false;

void OnMainStarted() {
  main_started = true;
}

/*!
 * JitMalloc calls malloc() and logs its argument and result into heap_objs.
 * @param[in]  ctx  
 */
void* JitMalloc(CONTEXT* ctx, AFUNPTR orig_func_ptr, size_t size) {
  void* ret;
  PIN_CallApplicationFunction(ctx, PIN_ThreadId(), CALLINGSTD_DEFAULT,
                              orig_func_ptr, nullptr,
                              PIN_PARG(void*), &ret,
                              PIN_PARG(size_t), size,
                              PIN_PARG_END());
  if (main_started) {
    heap_objs.emplace_back(reinterpret_cast<ADDRINT>(ret), size);
  }
  return ret;
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
  RTN rtn = TRACE_Rtn(trace);
  if (!RTN_Valid(rtn) || RTN_Id(rtn) != main_rtn_id) {
    return;
  }

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
 * ReplaceMalloc replaces malloc() with a wrapper, JitMalloc().
 * @param[in]  img  image to be instrumented
 */
VOID ReplaceMalloc(IMG img, VOID*) {
  RTN malloc_rtn = RTN_FindByName(img, "malloc");
  if (RTN_Valid(malloc_rtn)) {
    RTN_ReplaceSignature(malloc_rtn, reinterpret_cast<AFUNPTR>(JitMalloc),
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
  *out << "===============================================" << endl;
  *out << "Heap Objects:" << endl;
  for (auto& heap_obj : heap_objs) {
    *out << hex << " addr=0x" << heap_obj.addr
         << ", size=0x" << heap_obj.size << endl;
  }
  *out << "===============================================" << endl;
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

  if (!KnobOutputFile.Value().empty()) {
    out = new std::ofstream(KnobOutputFile.Value().c_str());
  }

  IMG_AddInstrumentFunction(ReplaceMalloc, 0);
  IMG_AddInstrumentFunction(InsertMainMarker, 0);
  TRACE_AddInstrumentFunction(ObserveMemAccess, 0);
  PIN_AddFiniFunction(Fini, 0);

  cerr << "===============================================" << endl;
  cerr << "This application is instrumented by Overflow" << endl;
  if (!KnobOutputFile.Value().empty()) {
    cerr << "See file " << KnobOutputFile.Value()
         << " for analysis results" << endl;
  }
  cerr << "===============================================" << endl;

  // Start the program, never returns
  PIN_StartProgram();

  return 0;
}
