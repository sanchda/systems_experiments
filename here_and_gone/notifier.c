#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/fanotify.h>
#include <string.h>
#include <errno.h>

#define EVENT_SIZE  (sizeof(struct fanotify_event_metadata))
#define BUFFER_SIZE 8192

#define FAN(X)      \
  X(ACCESS)         \
  X(DELETE)         \
  X(MODIFY)         \
  X(CLOSE_WRITE)    \
  X(CLOSE_NOWRITE)  \
  X(OPEN)

#define ADD_EVENT(name) mask |= FAN_##name;
#define MAKE_LUT(name) FAN_##name,
#define MAKE_ST(name) #name,
#define CASE(name) case FAN_##name: printf(#name"\n"); break;

const uint64_t lut[] = {
  FAN(MAKE_LUT)
};

const char *st[] = {
  FAN(MAKE_ST)
};

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  char *filename = argv[1];

  // Initialize fanotify
  int fan = fanotify_init(FAN_CLOEXEC, O_RDONLY);
  if (fan == -1) {
    perror("fanotify_init");
    exit(EXIT_FAILURE);
  }

  // Mark the target file for monitoring
  uint64_t mask = 0;
  FAN(ADD_EVENT);
  if (fanotify_mark(fan, FAN_MARK_ADD, mask, AT_FDCWD, "/home/ubuntu/dev/systems_experiments/here_and_gone") == -1) {
    perror("fanotify_mark");
    exit(EXIT_FAILURE);
  }

  // Fork and exec the given program
  pid_t pid = fork();
  if (pid == 0) { // Child
    char *child_argv[] = {filename, NULL};
    execvp(filename, child_argv);
    perror("execvp");
    exit(EXIT_FAILURE);
  } else if (pid < 0) {
    perror("fork");
    exit(EXIT_FAILURE);
  }

  // Parent process continues to monitor the file using fanotify
  char buffer[BUFFER_SIZE];
  while (1) {
    ssize_t len = read(fan, buffer, sizeof(buffer));
    if (len == -1 && errno != EAGAIN) {
      perror("read");
      exit(EXIT_FAILURE);
    }

    if (len <= 0) {
      break;
    }

    const struct fanotify_event_metadata *metadata;
    metadata = (struct fanotify_event_metadata *)buffer;

    while (FAN_EVENT_OK(metadata, len)) {
      switch(metadata->mask) {
        FAN(CASE);
      }
      metadata = FAN_EVENT_NEXT(metadata, len);
    }
  }

  close(fan);
  return 0;
}

