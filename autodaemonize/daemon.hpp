#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include <poll.h>
#include <sys/mman.h>

//==============================================================================
//                            Coordinator Primitives
//==============================================================================
template <class T> struct AtomicShared : public std::atomic<T> {
  static void *operator new(size_t) {
    // This is going to allocate at least a page for any primitive type, so don't use a lot of these.
    void *const pv = mmap(0, sizeof(T), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (pv == MAP_FAILED)
      throw std::bad_alloc();
    return pv;
  };

  static void operator delete(void *pv) {
    munmap(pv, sizeof(T));
  };

  AtomicShared &operator=(const int b) {
    std::atomic<T>::operator=(b);
    return *this;
  };

  // Block until the value has changes from specified, with timeout
  bool value_timedwait(const T &oldval, int timeout) {
    auto start = std::chrono::high_resolution_clock::now();
    auto end = std::chrono::milliseconds(timeout);
    if (timeout < 0)
      end = std::chrono::years::max(); // lol

    // Two-phase spinlock with three fast rebounds
    // after the fast rebounds, then start yielding
    // This is a naive implementation
    int fast_checks = 3;
    do {
      if (this->load() != oldval)
        return true;
      else if (fast_checks > 0)
        --fast_checks;
      else
        std::this_thread::yield();
    } while (std::chrono::high_resolution_clock::now() - start < end);

    return false;
  };
};

//==============================================================================
//                                    Daemon
//==============================================================================
enum DaemonState {
  Uninitialized,
  Initializing,
  Ready,
  Error,
};

class Daemon {
  AtomicShared<int> state;
  union {
    int _pfd[2];
    struct {
      int daemon_fd = -1;
      int client_fd = -1;
    };
  };

  // Doesn't handle fork errors, which it should do
  bool daemonize() {
    if (Ready == state.load())
      return true;

    close(client_fd); // just to be sure!
    state.store(Initializing);
    pipe(_pfd);
    if (!fork()) {
      if (fork())
        _exit(0);

      // Execution in grandchild (daemon)
      close(client_fd);
      state.store(Ready);
      run_daemon();
    }

    // Only the parent gets here
    state.value_timedwait(Initializing, 200);
    close(daemon_fd);
    return true;
  };

  [[noreturn]] bool run_daemon() {
    // Wakes up ever 100 ms just to check, this isn't a necessary part of the implementation
    struct pollfd poller = {daemon_fd, POLLIN | POLLHUP, 0};
    while (true) {
      switch (poll(&poller, 1, 100)) {
      case -1:
        // Whatever, just die
        _exit(0);
      case 0:
        std::cout << "[daemon] DAEMON HEARTBEAT, EVERYTHING OK" << std::endl;
        break;
      case 1:
        if (poller.revents & POLLHUP) {
          std::cout << "[daemon] Customer hung up!  Closing daemon!" << std::endl;
          _exit(0);
        } else {
          std::cout << "[daemon] GOT A MESSAGE" << std::endl;
          char buf[1024];
          read(daemon_fd, buf, sizeof(buf));
          std::cout << "-->" << buf << std::endl;
        }
        break;
      }
    }

    // Should never get here
    _exit(0);
  }

public:
  Daemon() {
    daemonize();
  }

  ~Daemon() {
    shutdown();
  }

  bool send(const std::string &msg) {
    return -1 != write(client_fd, msg.c_str(), msg.size());
  };

  void shutdown() {
    close(client_fd);
  };
};
