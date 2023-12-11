#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <unistd.h>


size_t g_buffer_size = 100*4096*1024; // 4MB
int g_fd = -1;
void *g_buffer = NULL;

__attribute__((constructor)) void init_memcpy_safe(void) {
  // Creates a temp file, unlinks it, mmaps with MAP_SHARED
  char *tmpfile = strdup("/tmp/memcpy_safe.XXXXXX");
  g_fd = mkstemp(tmpfile);
  unlink(tmpfile);
  free(tmpfile);
  if (g_fd == -1) {
    perror("mkstemp");
    return;
  }
  if (ftruncate(g_fd, g_buffer_size) == -1) {
    perror("ftruncate");
    return;
  }

  g_buffer = mmap(NULL, g_buffer_size, PROT_READ | PROT_WRITE, MAP_SHARED, g_fd, 0);
  if (g_buffer == MAP_FAILED) {
    perror("mmap");
    g_buffer = NULL;
    return;
  }
}

void *memget_safe(void *dst, size_t n) {
  // use `writev()` to safely copy data to the buffer
  struct iovec iov;
  iov.iov_base = g_buffer;
  iov.iov_len = n;
  if (writev(g_fd, &iov, 1) == -1) {
    return NULL;
  }
  return g_buffer;
}

bool memcpy_safe(void *dst, const void *src, size_t n) {
  if (g_buffer == NULL || n > g_buffer_size)
    return false;
  if (n == 0)
    return true;

  // Now copy from the buffer to the destination
  if (memget_safe(g_buffer, n) == NULL)
    return false;
  memcpy(dst, g_buffer, n);
  return true;
}
