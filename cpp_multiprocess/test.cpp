///usr/bin/g++-11 -std=c++20 $0 -o $(basename "$0" .cpp) && exec ./test
#include <atomic>
#include <chrono>
#include <iostream>
#include <new>
#include <thread>

#include <sys/mman.h>

template <class T>
class AtomicShared : public std::atomic<T> {
public:
  static void *operator new(size_t) {
    void *const pv = mmap(0, sizeof(T), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (pv == MAP_FAILED)
      throw std::bad_alloc();
    try {
      new (pv) std::atomic<T>{}; // perform any initialization the base class demands
    } catch (...) {
      munmap(pv, sizeof(T));
      throw std::bad_alloc();
    }
    return pv;
  };
  static void operator delete(void *pv) {
    munmap(pv, sizeof(T));
  };
  AtomicShared& operator=(const int b) {
    std::atomic<pid_t>::operator=(b);
    return *this;
  }

  bool timewait(const T& oldval, int timeout) {
    auto start = std::chrono::high_resolution_clock::now();
    do {
      if (this->load() != oldval)
        return true;
      std::this_thread::yield();
    } while (std::chrono::high_resolution_clock::now() - start < std::chrono::milliseconds(timeout));
    return false;
  }
  
};

int main() {
  std::unique_ptr<AtomicShared<pid_t>> parent = std::make_unique<AtomicShared<pid_t>>();
  std::unique_ptr<AtomicShared<pid_t>> child= std::make_unique<AtomicShared<pid_t>>();
  parent->store(0);
  child->store(0);
  if(fork()) {
    std::this_thread::sleep_for(std::chrono::seconds{5});
    parent->store(getpid());
  } else {
    child->store(getpid());
    std::cout << "Waiting" << std::endl;
    if (parent->timewait(0, 2000))
      std::cout << "Done waiting (passed)" << std::endl;
    else
      std::cout << "Done waiting (timed out)" << std::endl;
  }

  std::cout << "Spawned (" << getpid() << "), {" << parent->load() << ", " << child->load() << "}" << std::endl;
  std::cout << "Done (" << getpid() << "), {" << parent->load() << ", " << child->load() << "}" << std::endl;
  return 0;
}
