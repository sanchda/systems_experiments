#include <sys/wait.h>
#include <unistd.h>

#include <iostream>

#include "ring_buffer.hpp"

void my_iterator(std::string_view filename, std::string_view name, int64_t line) {
  std::cout << filename << ":" << name << ":" << line << std::endl;
}

void null_iterator(std::string_view filename, std::string_view name, int64_t line) {}

int main() {
  RingBuffer *_rb = new RingBuffer(1024);
  RingBuffer &rb = *_rb;

  // Now we're going to fork.  The parent will write to the ring buffer and the child will read from it
  pid_t pid = fork();
  if (pid == 0) {
    sleep(1);
    while (rb.read(my_iterator)) {
      std::cout << "Read" << std::endl;
    }
    std::cout << "Ring buffer empty" << std::endl;
    exit(0);
  } else {
    RBIn in;
    for (int i = 0; i < 10; i++) {
      in.push("Hello" + std::to_string(i), "Checkers" + std::to_string(i), i);
    }
    while (rb.write(in)) {
    }
    waitpid(pid, nullptr, 0);
  }

  if (rb.read(null_iterator)) {
    std::cout << "Ring buffer not empty after read" << std::endl;
    return 1;
  }

  // Bla bla bla cleanup, TBD
}
