#pragma once

#include "platform/platform.h"

namespace tcr::server {

plat::Mutex& core_lock();

class CoreLock {
 public:
  CoreLock();
  ~CoreLock();
  CoreLock(const CoreLock&) = delete;
  CoreLock& operator=(const CoreLock&) = delete;
};

bool holds_core_lock();

void run_hash_unlocked(void (*fn)(void*), void* arg);

}
