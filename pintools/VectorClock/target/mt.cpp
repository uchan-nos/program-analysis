#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

using namespace std;
atomic<int> x{0};
static const int kCount = 3;

#ifdef USE_LOCK
mutex m;
#endif

void f() {
  for (int i = 0; i < kCount; ++i) {
#ifdef USE_LOCK
    m.lock();
#endif
    int t = x.load();
    this_thread::sleep_for(chrono::milliseconds{10});
    x.store(t + 1);
#ifdef USE_LOCK
    m.unlock();
#endif
  }
}

int main() {
  thread t1{f}, t2{f};
  t1.join();
  t2.join();
  cout << "expected = " << 2 * kCount << endl;
  cout << "x        = " << x.load() << endl;
}
