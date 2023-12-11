#define _GNU_SOURCE

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include <signal.h>
#include <unistd.h>

#include <sys/uio.h>

#include "memcpy_safe.h"

void print_throughput(struct timespec start, struct timespec end, size_t bytes, const char* msg_prefix) {
  double time_diff = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
  double throughput = bytes / time_diff / (1024 * 1024 * 1024);
  printf("%s: %.2f GB/s\n", msg_prefix, throughput);
}

#define RUN_THROUGHPUT(func, dst, src, sz, print)     \
{                                                     \
  struct timespec start, end;                         \
  clock_gettime(CLOCK_MONOTONIC, &start);             \
  if (!func(dst, src, sz)) {                          \
    printf("Test failed: %s\n", #func);               \
    return;                                           \
  }                                                   \
  clock_gettime(CLOCK_MONOTONIC, &end);               \
  if (print)                                          \
    print_throughput(start, end, sz, "[T]("#func")"); \
}

#define RUN_LATENCY(func, dst, src, sz, iter, print)         \
{                                                            \
  struct timespec start, end;                                \
  clock_gettime(CLOCK_MONOTONIC, &start);                    \
  for (size_t i = 0; i < iter; i++) {                        \
    if (!func(dst, src, sz)) {                               \
      printf("Test failed: %s\n", #func);                    \
      return;                                                \
    }                                                        \
  }                                                          \
  clock_gettime(CLOCK_MONOTONIC, &end);                      \
  if (print)                                                 \
    print_throughput(start, end, sz * iter, "[L]("#func")"); \
}

bool test_read(void *dst, void *_, size_t sz) {
  volatile bool flag = true;
  for (size_t i = 0; i < sz; i++) {
    if (((char*)dst)[i] != ((char*)dst)[i])
      flag = false;
  }
  flag = true;
  return flag;
}

bool test_memcpy(void *dst, void *src, size_t sz) {
  bool flag = true;
  memcpy(dst, src, sz);
  for (size_t i = 0; i < sz; i++) {
    if (((char*)dst)[i] != ((char*)src)[i])
      flag = false;
  }
  return flag;
}

bool test_memcpy_safe(void *dst, void *src, size_t sz) {
  bool flag = true;
  memcpy_safe(dst, src, sz);
  for (size_t i = 0; i < sz; i++) {
    if (((char*)dst)[i] != ((char*)src)[i])
      flag = false;
  }
  return flag;
}

bool test_memget_safe(void *_, void *src, size_t sz) {
  bool flag = true;
  char *dst = memget_safe(src, sz);
  for (size_t i = 0; i < sz; i++) {
    if (((char*)dst)[i] != ((char*)src)[i])
      flag = false;
  }
  return flag;
}

bool test_process_vm_readv(void *dst, void *src, size_t sz) {
  static struct iovec local[1];
  static struct iovec remote[1];
  bool flag = true;
  local[0].iov_base = dst;
  local[0].iov_len = sz;
  remote[0].iov_base = src;
  remote[0].iov_len = sz;
  process_vm_readv(getpid(), local, 1, remote, 1, 0);
  for (size_t i = 0; i < sz; i++) {
    if (((char*)dst)[i] != ((char*)src)[i])
      flag = false;
  }
  return flag;
}

// Sets a sigsegv handler and then returns false if during traversal of the data, a segfault occurs
bool g_segfault_flag = true;
void segfault_handler(int sig) {
  g_segfault_flag = false;
}
bool test_segfault(void *dst, void *src, size_t sz) {
  bool flag = true;

  // Setup handler
  struct sigaction sa;
  sa.sa_handler = segfault_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGSEGV, &sa, NULL);

  // Iterate
  for (size_t i = 0; i < sz; i++) {
    if (((char*)dst)[i] != ((char*)src)[i])
      flag = false;
  }

  // Teardown handler
  sa.sa_handler = SIG_DFL; // Should actually restore
  sigaction(SIGSEGV, &sa, NULL);
  return g_segfault_flag && flag;
}

void throughput_tests(bool print) {
  // Time to test memcpy_safe on 100mb of data in one block
  size_t sz = 100 * 1024 * 1024;
  char *data = malloc(sz);
  char *dst = malloc(sz);

  // Tests
  RUN_THROUGHPUT(test_read, dst, data, sz, print);
  RUN_THROUGHPUT(test_memcpy, dst, data, sz, print);
  RUN_THROUGHPUT(test_memget_safe, dst, data, sz, print);
  RUN_THROUGHPUT(test_memcpy_safe, dst, data, sz, print);
  RUN_THROUGHPUT(test_process_vm_readv, dst, data, sz, print);
  RUN_THROUGHPUT(test_segfault, dst, data, sz, print);

  // Cleanup
  free(data);
  free(dst);
}

void latency_tests(bool print) {
  // Unlike the throughput tests, these run on a single page in a loop that copies a total of 100mb
  size_t sz = 4096;
  size_t iter = 100 * 1024 * 1024 / sz;
  char *data = malloc(sz);
  char *dst = malloc(sz);

  // Tests
  RUN_LATENCY(test_read, dst, data, sz, iter, print);
  RUN_LATENCY(test_memcpy, dst, data, sz, iter, print);
  RUN_LATENCY(test_memget_safe, dst, data, sz, iter, print);
  RUN_LATENCY(test_memcpy_safe, dst, data, sz, iter, print);
  RUN_LATENCY(test_process_vm_readv, dst, data, sz, iter, print);
  RUN_LATENCY(test_segfault, dst, data, sz, iter, print);

  free(data);
  free(dst);
}

int main() {
  // Warmup
  throughput_tests(false);
  throughput_tests(false);
  throughput_tests(false);

  // Run the test suites
  throughput_tests(true);
  printf("\n");
  latency_tests(true);
  return 0;
}
