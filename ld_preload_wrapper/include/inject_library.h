#pragma once
#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

typedef struct LdPreloadCtx {
  int fd;
  char *addr;
  size_t sz;
} LdPreloadCtx;

static void destroy_ctx(LdPreloadCtx *ctx) {
  munmap(ctx->addr, ctx->sz);
  close(ctx->fd);
}

static bool map_ctx(LdPreloadCtx *ctx, const char *start, const char *end) {
  ctx->sz = end - start;
  ctx->fd = memfd_create("peekenv", 0);
  if (-1 == ctx->fd) {
    fprintf(stderr, "Cannot create memfd\n");
    return false;
  }

  if (-1 == ftruncate(ctx->fd, ctx->sz)) {
    fprintf(stderr, "Cannot truncate memfd\n");
    destroy_ctx(ctx);
    return false;
  }

  // Create a memfd
  ctx->addr = mmap(NULL, ctx->sz, PROT_READ | PROT_WRITE, MAP_SHARED, ctx->fd, 0);
  if (MAP_FAILED == ctx->addr) {
    fprintf(stderr, "Cannot mmap memfd\n");
    destroy_ctx(ctx);
    return false;
  }
  memcpy(ctx->addr, start, ctx->sz);
  return true;
}

// Generic helper function for adding a start/end pair to LD_PRELOAD
// NOTE: this will go away when the holding process dies.
bool add_to_ld_preload(const char *start, const char *end) {
  LdPreloadCtx ctx;
  if (!map_ctx(&ctx, start, end)) {
    return false;
  }

  // Finally, format the path to the fd for use in LD_PRELOAD
  const char *fmt = "/proc/%d/fd/%d";
  char fd_path[sizeof(fmt) + 10 + 10]; // let's say 10 is enough for the fd
  {
    size_t n = snprintf(fd_path, sizeof(fd_path), fmt, getpid(), ctx.fd);
    if (n >= sizeof(fd_path)) {
      fprintf(stderr, "FD path too long\n");
      destroy_ctx(&ctx);
      return false;
    }
  }

  // Setup the environment.  We have to add to LD_PRELOAD
  // 1. if it's set, the append
  // 2. if it's not set, just set it
  char *ld_preload = getenv("LD_PRELOAD");
  if (ld_preload) {
    char new_ld_preload[4096];
    size_t n = snprintf(new_ld_preload, sizeof(new_ld_preload), "%s:%s", ld_preload, fd_path);
    if (n >= sizeof(new_ld_preload)) {
      fprintf(stderr, "LD_PRELOAD too long\n");
      destroy_ctx(&ctx);
      return false;
    }
    setenv("LD_PRELOAD", new_ld_preload, 1);
  } else {
    setenv("LD_PRELOAD", fd_path, 0);
  }

  return true;
}
