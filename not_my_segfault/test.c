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

void throughput_tests(bool print) {
  // Time to test memcpy_safe on 100mb of data in one block
  size_t sz = 100 * 1024 * 1024;
  char *data = malloc(sz);
  char *dst = malloc(sz);

  // Throw away one memget (warmup?)
  memget_safe(data, sz);

  // Click a timer
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);

  // Copy into the buffer
  memget_safe(data, sz);

  // Click the timer again
  clock_gettime(CLOCK_MONOTONIC, &end);
  if (print)
    print_throughput(start, end, sz, "[T](write-static)");

  // Now try the full copy operation
  clock_gettime(CLOCK_MONOTONIC, &start);
  memcpy_safe(dst, data, sz);
  clock_gettime(CLOCK_MONOTONIC, &end);
  if (print)
    print_throughput(start, end, sz, "[T](write-memcpy)");

  // Try process_vm_readv
  clock_gettime(CLOCK_MONOTONIC, &start);
  struct iovec local[1];
  struct iovec remote[1];
  local[0].iov_base = dst;
  local[0].iov_len = sz;
  remote[0].iov_base = data;
  remote[0].iov_len = sz;
  process_vm_readv(getpid(), local, 1, remote, 1, 0);
  clock_gettime(CLOCK_MONOTONIC, &end);
  if (print)
    print_throughput(start, end, sz, "[T](process_vm_readv)");

  free(data);
  free(dst);
}

void latency_tests(bool print) {
  // Unlike the throughput tests, these run on a single page in a loop that copies a total of 100mb
  size_t sz = 4096;
  size_t iter = 100 * 1024 * 1024 / sz;
  char *data = malloc(sz);
  char *dst = malloc(sz);

  // Throw away one memget (warmup?)
  memget_safe(data, sz);

  // Click a timer
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);

  // Copy into the buffer
  for (int i = 0; i < iter; i++)
    memget_safe(data, sz);
  clock_gettime(CLOCK_MONOTONIC, &end);
  if (print)
    print_throughput(start, end, sz*iter, "[L](write-static)");

  // Now try the full copy operation
  clock_gettime(CLOCK_MONOTONIC, &start);
  for (int i = 0; i < iter; i++)
    memcpy_safe(dst, data, sz);
  clock_gettime(CLOCK_MONOTONIC, &end);
  if (print)
    print_throughput(start, end, sz*iter, "[L](write-memcpy)");

  // Try process_vm_readv
  clock_gettime(CLOCK_MONOTONIC, &start);
  struct iovec local[1];
  struct iovec remote[1];
  local[0].iov_base = dst;
  local[0].iov_len = sz;
  remote[0].iov_base = data;
  remote[0].iov_len = sz;
  for (int i = 0; i < iter; i++)
    process_vm_readv(getpid(), local, 1, remote, 1, 0);
  clock_gettime(CLOCK_MONOTONIC, &end);
  if (print)
    print_throughput(start, end, sz*iter, "[L](process_vm_readv)");

  free(data);
  free(dst);
}

int main() {
  throughput_tests(false);
  throughput_tests(false);
  throughput_tests(false);
  throughput_tests(true);
  latency_tests(true);
  return 0;
}
