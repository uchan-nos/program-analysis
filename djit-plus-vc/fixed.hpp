#include <array>
#include <functional>
#include <map>
#include <vector>

struct Variable {
  std::string name;
};

bool operator <(const Variable& lhs, const Variable& rhs) {
  return lhs.name < rhs.name;
}

struct Lock {
  std::string name;
};

bool operator <(const Lock& lhs, const Lock& rhs) {
  return lhs.name < rhs.name;
}

template <size_t N>
struct FixedVectorClock {
  std::array<int, N> clocks{};

  const int& operator [](size_t i) const {
    return clocks[i];
  }
  int& operator [](size_t i) {
    return clocks[i];
  }
};

template <size_t N>
FixedVectorClock<N> operator |=(FixedVectorClock<N>& lhs,
                                const FixedVectorClock<N>& rhs) {
  for (size_t i = 0; i < N; ++i) {
    lhs[i] = std::max(lhs[i], rhs[i]);
  }
  return lhs;
}

template <size_t N>
FixedVectorClock<N> operator |(const FixedVectorClock<N>& lhs,
                               const FixedVectorClock<N>& rhs) {
  auto merged = lhs;
  merged |= rhs;
  return merged;
}

template <size_t N>
bool operator <=(const FixedVectorClock<N>& lhs,
                 const FixedVectorClock<N>& rhs) {
  for (int i = 0; i < N; ++i) {
    if (lhs[i] > rhs[i]) {
      return false;
    }
  }
  return true;
}

template <size_t N>
bool operator >(const FixedVectorClock<N>& lhs,
                const FixedVectorClock<N>& rhs) {
  return !(lhs <= rhs);
}

template <size_t NThread>
class Analyzer {
 public:
  Analyzer() : thread_vc_{}, read_vc_{}, write_vc_{}, lock_vc_{} {
    for (int i = 0; i < NThread; ++i) {
      thread_vc_[i][i] = 1;
    }
  }

  Analyzer& Read(int t, const Variable& x) {
    read_vc_[x][t] = thread_vc_[t][t];
    if (write_vc_[x] > thread_vc_[t]) {
      if (on_read_violated_) {
        on_read_violated_(*this, t, x);
      }
    }
    return *this;
  }
  Analyzer& Write(int t, const Variable& x) {
    write_vc_[x][t] = thread_vc_[t][t];
    if (write_vc_[x] > thread_vc_[t] || read_vc_[x] > thread_vc_[t]) {
      if (on_write_violated_) {
        on_write_violated_(*this, t, x);
      }
    }
    return *this;
  }
  Analyzer& Acquire(int t, const Lock& m) {
    thread_vc_[t] |= lock_vc_[m];
    return *this;
  }
  Analyzer& Release(int t, const Lock& m) {
    ++thread_vc_[t][t];
    lock_vc_[m] = thread_vc_[t];
    return *this;
  }

  Analyzer& Register(const Variable& x) {
    variables_.push_back(x);
    read_vc_.emplace(x, FixedVectorClock<NThread>{});
    write_vc_.emplace(x, FixedVectorClock<NThread>{});
    return *this;
  }
  Analyzer& Register(const Lock& m) {
    locks_.push_back(m);
    lock_vc_.emplace(m, FixedVectorClock<NThread>{});
    return *this;
  }

  const std::vector<Variable> GetVariables() const {
    return variables_;
  }
  const std::vector<Lock> GetLocks() const {
    return locks_;
  }

  const FixedVectorClock<NThread>& GetThreadVC(int t) const {
    return thread_vc_.at(t);
  }
  const FixedVectorClock<NThread>& GetReadVC(const Variable& x) const {
    return read_vc_.at(x);
  }
  const FixedVectorClock<NThread>& GetWriteVC(const Variable& x) const {
    return write_vc_.at(x);
  }
  const FixedVectorClock<NThread>& GetLockVC(const Lock& m) const {
    return lock_vc_.at(m);
  }

  using ViolationHandler = std::function<
    void (const Analyzer<NThread>&, int t, const Variable&)
  >;

  Analyzer& SetReadViolationHandler(const ViolationHandler& f) {
    on_read_violated_ = f;
    return *this;
  }
  Analyzer& SetWriteViolationHandler(const ViolationHandler& f) {
    on_write_violated_ = f;
    return *this;
  }

 private:
  std::array<FixedVectorClock<NThread>, NThread> thread_vc_;
  std::map<Variable, FixedVectorClock<NThread>> read_vc_, write_vc_;
  std::map<Lock, FixedVectorClock<NThread>> lock_vc_;

  std::vector<Variable> variables_;
  std::vector<Lock> locks_;

  ViolationHandler on_read_violated_, on_write_violated_;
};
