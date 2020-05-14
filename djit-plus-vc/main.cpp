#include <iostream>

#include "fixed.hpp"

template <size_t NThread>
void PrintHeader(const Analyzer<NThread>& a) {
  std::cout << "C0";
  for (int t = 1; t < NThread; ++t) {
    std::cout << "\tC" << t;
  }
  for (const Variable& x : a.GetVariables()) {
    std::cout << "\tR" << x.name << "\tW" << x.name;
  }
  for (const Lock& m : a.GetLocks()) {
    std::cout << "\tL" << m.name;
  }
  std::cout << std::endl;
}

template <size_t N>
void PrintVC(const FixedVectorClock<N>& vc) {
  std::cout << "<" << vc[0];
  for (int i = 1; i < N; ++i) {
    std::cout << "," << vc[i];
  }
  std::cout << ">";
}

template <size_t NThread>
void PrintVCs(const Analyzer<NThread>& a) {
  PrintVC(a.GetThreadVC(0));
  for (int t = 1; t < NThread; ++t) {
    std::cout << "\t";
    PrintVC(a.GetThreadVC(t));
  }
  for (const Variable& x : a.GetVariables()) {
    std::cout << "\t";
    PrintVC(a.GetReadVC(x));
    std::cout << "\t";
    PrintVC(a.GetWriteVC(x));
  }
  for (const Lock& m : a.GetLocks()) {
    std::cout << "\t";
    PrintVC(a.GetLockVC(m));
  }
  std::cout << std::endl;
}

const int kNumThread = 2;

//#define PROTECT_BY_LOCK

int main() {
  Analyzer<kNumThread> a;
  a.SetReadViolationHandler(
    [](const auto& an, int t, const auto& x) {
      std::cout << "race condition detected: rd("
                << t << "," << x.name << ")" << std::endl;
    });
  a.SetWriteViolationHandler(
    [](const auto& an, int t, const auto& x) {
      std::cout << "race condition detected: wr("
                << t << "," << x.name << ")" << std::endl;
    });

  Variable x{"x"};
  Lock m{"m"};
  a.Register(x);
  a.Register(m);

  auto rd = [&](int t, const Variable& x) {
    std::cout << "rd(" << t << "," << x.name << ")" << std::endl;
    a.Read(t, x);
    PrintVCs(a);
  };
  auto wr = [&](int t, const Variable& x) {
    std::cout << "wr(" << t << "," << x.name << ")" << std::endl;
    a.Write(t, x);
    PrintVCs(a);
  };
  auto acq = [&](int t, const Lock& m) {
    std::cout << "acq(" << t << "," << m.name << ")" << std::endl;
    a.Acquire(t, m);
    PrintVCs(a);
  };
  auto rel = [&](int t, const Lock& m) {
    std::cout << "rel(" << t << "," << m.name << ")" << std::endl;
    a.Release(t, m);
    PrintVCs(a);
  };

  PrintHeader(a);
  PrintVCs(a);

#ifdef PROTECT_BY_LOCK
  acq(0, m);
  rd(0, x);
  wr(0, x);
  rel(0, m);
  acq(1, m);
  rd(1, x);
  wr(1, x);
  rel(1, m);
#else
  rd(0, x);
  rd(1, x);
  wr(0, x);
  wr(1, x);
#endif
}
