#include <iostream>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <sys/types.h>

struct Foo {
  inline static std::mutex m;

  void spin() {
    while (true)
      std::lock_guard<std::mutex> lock(m);
  }

  // Spawn a thread to spin
  void spin_thread(int n) {
    while (--n >= 0) {
      std::cout << "Spawning thread from PID " << getpid() << std::endl;
      std::lock_guard<std::mutex> lock(m);
      std::thread t(&Foo::spin, this);
      t.detach();
    }
  }
};


void fork_n_go() {
  pid_t pid = fork();
  if (!pid) {
    Foo foo;
    foo.spin_thread(10);
  }
  sleep(0.1);
}

int main() {
  Foo f;
  f.spin_thread(10);

  // fork and go
  for (int i = 0; i < 5; ++i)
    fork_n_go();

  // sleep for 1 second
  sleep(1);
  return 0;
}
