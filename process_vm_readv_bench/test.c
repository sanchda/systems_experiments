#define _GNU_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#define MAX_ARENAS 128
#define ARENA_SIZE 0x1000

#ifndef MTUNE
#define MTUNE "unknown"
#endif

unsigned char *arena[MAX_ARENAS];
unsigned char *source;
pid_t pid = 0;
__attribute__((constructor)) void init() {
  for (int i = 0; i < MAX_ARENAS; i++) {
    arena[i] = mmap(0, ARENA_SIZE, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (arena[i] == MAP_FAILED) {
      fprintf(stderr, "mmap failed\n");
      exit(1);
    }
  }

  source = mmap(0, ARENA_SIZE * MAX_ARENAS, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (source == MAP_FAILED) {
    fprintf(stderr, "mmap failed\n");
    exit(1);
  }

  pid = getpid();
}

bool test_process_vm_readv(int copies, void *src, size_t sz) {
  static struct iovec local[MAX_ARENAS];
  static struct iovec remote[MAX_ARENAS];
  for (int i = 0; i < copies; i++) {
    local[i].iov_base = arena[i];
    local[i].iov_len = sz;
    remote[i].iov_base = src + i * sz;
    remote[i].iov_len = sz;
  }
  return sz * copies == process_vm_readv(pid, local, copies, remote, copies, 0);
}

uint64_t run_trial(int copies, size_t sz) {
  uint64_t start = __builtin_ia32_rdtsc();
  return !test_process_vm_readv(copies, source, sz)
             ? UINT64_MAX
             : __builtin_ia32_rdtsc() - start;
}

void print_trial(int copies, size_t sz) {
  uint64_t cycles = run_trial(copies, sz);
  uint64_t bytes = copies * sz;
  double cycles_per_byte = (double)cycles / (double)bytes;

  // print in CSV format (truncate floats to 3 decimal places)
  printf("%s,%d,%zu,%zu,%zu,%.3f\n", MTUNE, copies, sz, bytes, cycles, cycles_per_byte);
}

void print_header() { printf("mtune,copies,sz,bytes,cycles,cycles_per_byte\n"); }

int main(int argc, char **argv) {
  if (argc > 1 && strcmp(argv[1], "header") == 0) {
    print_header();
    return 0;
  }

  for (int i = 0; i < 100; i++) {
    for (int copies = 1; copies <= 128; copies *= 2) {
      for (size_t sz = 1; sz <= 0x1000; sz *= 2) {
        print_trial(copies, sz);
      }
    }
  }
  return 0;
}
