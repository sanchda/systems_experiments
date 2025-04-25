#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

char super_secret_message[] = "Hello, this is a super secret message!\n";
size_t super_secret_message_len = sizeof(super_secret_message);

bool create_child_and_wait(bool use_ptracer) {
  pid_t pid = fork();
  if (pid == -1) {
    perror("fork");
    return false;
  }

  pid_t ppid = getpid(); // For the child
  if (pid == 0) {
    // Child
    // Wait for 500 ms
    usleep(500000);

    // This is weird, but we're going to read the super secret message, which we
    // know the address of, after an execve. First, set up the args
    char ppid_str[32];
    char ptr_str[32];
    snprintf(ppid_str, sizeof(ppid_str), "%d", ppid);
    snprintf(ptr_str, sizeof(ptr_str), "%p", super_secret_message);
    char *args[] = {NULL, ppid_str, ptr_str, NULL};
    args[0] = "/proc/self/exe"; // coward's way out

    printf("The secret message is at 0x%p\n", super_secret_message);
    if (-1 == execve(args[0], args, NULL)) {
      perror("execve");
      return false;
    }

    // We should never get here
    printf("Child FAILED execve\n");
    return false;

  } else {
    // Parent
    if (use_ptracer && -1 == prctl(PR_SET_PTRACER, pid, 0, 0, 0)) {
      perror("prctl");
      return false;
    }

    // Wait for the child to stop.  In full generality, scraping the process
    // state transition is weird, so just wait for -1
    while (true) {
      if (waitpid(pid, NULL, 0) == -1) {
        break;
      }
    }
  }
}

// Use process_vm_readv to read the magically known number of bytes from the given process at the given
// address, copying into a buffer.
ssize_t process_vm_readv(pid_t, const struct iovec *, unsigned long,
                         const struct iovec *, unsigned long, unsigned long);
char copy_buffer[sizeof(super_secret_message)];
bool read_remote_buffer(pid_t ppid, uint64_t ptr) {
  struct iovec iov_dst = {copy_buffer, sizeof(copy_buffer)};
  struct iovec iov_src = {(void *)ptr, sizeof(copy_buffer)};
  ssize_t res = process_vm_readv(ppid, &iov_dst, 1, &iov_src, 1, 0);

  return 0 <= res;
}

void print_secret_message(pid_t ppid, uint64_t ptr) {
  // Use process_vm_readv
  if (read_remote_buffer(ppid, ptr)) {
    printf("Secret message: %s\n", copy_buffer);
  } else {
    if (errno == EPERM) {
      printf("Failed to read secret message: EPERM\n");
    } else if (errno == EFAULT) {
      printf("Failed to read secret message: EFAULT\n");
    } else {
      perror("Failed to read secret message");
    }
    printf("Failed to read secret message\n");
  }
}

int main(int argc, char **argv) {
  // If I'm given zero arguments, then assume I should use the prctl.
  // If I'm given one argument, then assume it's a bool indicating whether or
  // not to use the prctl. If I'm given more than two, assume the first is a
  // PID, and the second is an address to read remotely as a null-terminated
  // string.
  if (argc == 1) {
    create_child_and_wait(true);
  } else if (argc == 2) {
    if (strcmp(argv[1], "true") == 0) {
      create_child_and_wait(true);
    } else {
      create_child_and_wait(false);
    }
  } else if (argc == 3) {
    pid_t ppid = atoi(argv[1]);
    uint64_t ptr = strtoull(argv[2], NULL, 0);
    printf("Going to read from PID %d at address %p\n", ppid, (void *)ptr);
    print_secret_message(ppid, ptr);
  } else {
    printf("Usage: %s [use_ptracer] [pid] [address]\n", argv[0]);
    return 1;
  }

  return 0;
}
