#include <array>
#include <iostream>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>

struct ThreadArgs {
  int index;
  pthread_t* tid_array;
};

void *print_tid(void *arg) {
  ThreadArgs *args = static_cast<ThreadArgs *>(arg);
  args->tid_array[args->index] = gettid();
  return nullptr;
}

int main() {
  // Allocate storage
  pid_t* pid_array = (pid_t *)mmap(nullptr, 10*sizeof(pid_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  pthread_t* tid_array = (pthread_t *)mmap(nullptr, 10*sizeof(pthread_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);


  // Spawn 10 processes via fork() and print their pids
  std::cout << "Spawning children via `fork()`" << std::endl;
  for (int i = 0; i < 10; ++i) {
    pid_t pid = fork();
    if (!pid)
      std::exit(0);
    else
      pid_array[i] = pid;
  }

  // Need to make sure all the children are gone in case I need to wait later
  for (int i = 0; i < 10; ++i) {
    std::cout << "PID: " << pid_array[i] - getpid() << std::endl;
    wait(nullptr);
  }

  // Spawn 10 threads
  std::cout << "\n\nSpawning threads via `pthread_create()`" << std::endl;
  std::array<pthread_t, 10> threads;
  std::array<ThreadArgs, 10> args;
  for (int i = 0; i < 10; ++i) {
    args[i] = {i, tid_array};
    pthread_create(&threads[i], nullptr, print_tid, &args[i]);
  }

  // Join and print
  for (int i = 0; i < 10; ++i) {
    pthread_join(threads[i], nullptr);
    std::cout << "TID: " << tid_array[i] - gettid() << std::endl;
  }

  // Interleaved spawning
  std::cout << "\n\nSpawning interleaved forks and threads" << std::endl;
  for (int i = 0; i < 10; ++i) {
    pid_t pid = fork();
    if (!pid) {
      pthread_t thread;
      pthread_create(&threads[i], nullptr, print_tid, &args[i]);
      pthread_join(threads[i], nullptr);
      std::exit(0);
    } else {
      pid_array[i] = pid;
      wait(nullptr);
    }
  }

  // Done, so print
  for (int i = 0; i < 10; ++i) {
    std::cout << "PID: " << pid_array[i] - getpid() << std::endl;
    std::cout << "TID: " << tid_array[i] - gettid() << std::endl;
  }

  return 0;
}
