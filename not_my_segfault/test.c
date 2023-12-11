#define _GNU_SOURCE

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include <sys/uio.h>

#include "memcpy_safe.h"

void print_throughput(struct timespec start, struct timespec end, size_t bytes, const char* msg_prefix) {
  double time_diff = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
  double throughput = bytes / time_diff / (1024 * 1024 * 1024);
  printf("%s Throughput: %.2f GB/s\n", msg_prefix, throughput);
}

typedef struct Dangerous {
  int *first;
  int *second;
  int *third;
  int *fourth;
  int *fifth;
} Dangerous;

static Dangerous d[10];

__attribute__((constructor)) void init() {
  // Populate each of the Dangerous structs so that the ints are randomly
  // allocated
  for (int i = 0; i < sizeof(d) / sizeof(d[0]); i++) {
    d[i].first = malloc(sizeof(int));
    d[i].second = malloc(sizeof(int));
    d[i].third = malloc(sizeof(int));
    d[i].fourth = malloc(sizeof(int));
    d[i].fifth = malloc(sizeof(int));
    switch (rand() % 35) {
      case 0:
        free(d[i].first); d[i].first = NULL;
        break;
      case 1:
        free(d[i].second); d[i].second = NULL;
        break;
      case 2:
        free(d[i].third); d[i].third = NULL;
        break;
      case 3:
        free(d[i].fourth); d[i].fourth = NULL;
        break;
      case 4:
        free(d[i].fifth); d[i].fifth = NULL;
        break;
    }
  }
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

// Runs a test on a single function
#define RUN_THROUGHPUT(func, dst, src, sz, msg, print) \
{                                                      \
  struct timespec start, end;                          \
  clock_gettime(CLOCK_MONOTONIC, &start);              \
  if (!func(dst, src, sz)) {                           \
    printf("Test failed: %s\n", #func);                \
    return;                                            \
  }                                                    \
  clock_gettime(CLOCK_MONOTONIC, &end);                \
  if (print)                                           \
    print_throughput(start, end, sz, msg #func);       \
}

#define RUN_LATENCY(func, dst, src, sz, iter, msg, print) \
{                                                         \
  struct timespec start, end;                             \
  clock_gettime(CLOCK_MONOTONIC, &start);                 \
  for (size_t i = 0; i < iter; i++) {                     \
    if (!func(dst, src, sz)) {                            \
      printf("Test failed: %s\n", #func);                 \
      return;                                             \
    }                                                     \
  }                                                       \
  clock_gettime(CLOCK_MONOTONIC, &end);                   \
  if (print)                                              \
    print_throughput(start, end, sz, msg #func);          \
}

void throughput_tests(bool print) {
  // Time to test memcpy_safe on 100mb of data in one block
  size_t sz = 100 * 1024 * 1024;
  char *data = malloc(sz);
  char *dst = malloc(sz);

  // Tests
  RUN_THROUGHPUT(test_read, dst, data, sz, "[T](read)", print);
  RUN_THROUGHPUT(test_memcpy, dst, data, sz, "[T](memcpy)", print);
  RUN_THROUGHPUT(test_memget_safe, dst, data, sz, "[T](memget)", print);
  RUN_THROUGHPUT(test_memcpy_safe, dst, data, sz, "[T](memcpy_safe)", print);
  RUN_THROUGHPUT(test_process_vm_readv, dst, data, sz, "[T](process_vm_readv)", print);

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
  RUN_LATENCY(test_read, dst, data, sz, iter, "[T](read)", print);
  RUN_LATENCY(test_memcpy, dst, data, sz, iter, "[T](memcpy)", print);
  RUN_LATENCY(test_memget_safe, dst, data, sz, iter, "[T](memget)", print);
  RUN_LATENCY(test_memcpy_safe, dst, data, sz, iter, "[T](memcpy_safe)", print);
  RUN_LATENCY(test_process_vm_readv, dst, data, sz, iter, "[T](process_vm_readv)", print);

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
  latency_tests(true);
  return 0;
}
