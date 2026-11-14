#include "server/core_lock.h"

#include <thread>

namespace tcr::server {

plat::Mutex& core_lock() {
  static plat::Mutex m;
  return m;
}

namespace {

thread_local int t_held = 0;

}

CoreLock::CoreLock() {
  core_lock().lock();
  ++t_held;
}

CoreLock::~CoreLock() {
  --t_held;
  core_lock().unlock();
}

bool holds_core_lock() { return t_held > 0; }

namespace {

plat::Mutex& slots_lock() {
  static plat::Mutex m;
  return m;
}
unsigned g_hashing = 0;

unsigned hash_threads() {
  unsigned n = std::thread::hardware_concurrency();
  return n == 0 ? 2 : (n < 4 ? n : 4);
}

}

void run_hash_unlocked(void (*fn)(void*), void* arg) {
  if (t_held == 0) {
    fn(arg);
    return;
  }
  int held = t_held;
  t_held = 0;
  core_lock().unlock();
  while (true) {
    {
      plat::Lock lock(slots_lock());
      if (g_hashing < hash_threads()) {
        ++g_hashing;
        break;
      }
    }
    plat::sleep_seconds(0.005);
  }
  fn(arg);
  {
    plat::Lock lock(slots_lock());
    --g_hashing;
  }
  core_lock().lock();
  t_held = held;
}

}
