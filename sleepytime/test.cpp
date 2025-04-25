#include <chrono>
#include <thread>
#include <iostream>

struct SleepyTimer {
  std::size_t duration; // micros per million
  SleepyTimer() {
    // Time 1e6 iterations
    // Note that the underlying x86/ARM hardwares are not homogeneous in the sens that every one instruction
    // consumes the same type of shared resources.  Read/write/integral/float/etc operations use similar
    // and also different hardware; it depends on load, it depends on sequencing, etc.  Our goal here is
    // to implement a function which will be on-CPU for its duration, even in pathological configs where
    // checking a timer involves a context-switch.
    auto start = std::chrono::system_clock::now().time_since_epoch();
    for (std::size_t i = 0; i < 1e9; ++i) {
      volatile std::size_t _ = 0;
      _ = _ + 1;
    }
    auto end = std::chrono::system_clock::now().time_since_epoch();
    duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  }
};


void little_sleep(std::chrono::microseconds us) {
  auto start = std::chrono::high_resolution_clock::now();
  auto end = start + us;
  do {
      std::this_thread::yield();
  } while (std::chrono::high_resolution_clock::now() < end);
}


int main() {
  SleepyTimer timer = {};
  for (std::size_t i = 0; i < 1e4; i++) {
    little_sleep(std::chrono::microseconds(i));
  }
  return 0;
}
