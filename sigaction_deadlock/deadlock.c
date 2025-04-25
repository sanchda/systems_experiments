#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void do_segfault() {
  int *p = 0;
  *p = 0;
}

bool segfault_in_segfault = false;
bool use_static_check = false;

void segfault_handler(int signum) {
  static const char msg[] = "Segmentation fault\n";
  static bool is_first = true;

  if (use_static_check && is_first) {
    // Keep going
    is_first = false;
    static const char msg2[] = "Static check\n";
    write(1, msg2, sizeof(msg2));
  } else if (use_static_check && !is_first) {
    // stop!
    static const char msg3[] = "Static check failed\n";
    write(1, msg3, sizeof(msg3));
    exit(1);
  }

  write(1, msg, sizeof(msg));
  if (segfault_in_segfault) {
    static const char msg2[] = "Double fault\n";
    write(1, msg2, sizeof(msg2));
    do_segfault();
  }
  static const char exit_msg[] = "Exiting\n";
  write(1, exit_msg, sizeof(exit_msg));
  exit(1);
}

bool user_wants_thing(int argc, char **argv, const char **patterns, size_t size) {
  for (int i = 1; i < argc; i++) {
    for (int j = 0; j < size; j++) {
      if (strcmp(argv[i], patterns[j]) == 0) {
        return true;
      }
    }
  }
  return false;
}

bool user_needs_help(int argc, char **argv) {
  static const char *help_patterns[] = {"help", "-help", "--help", "-h", "?"};
  static const size_t hp_size =
      sizeof(help_patterns) / sizeof(help_patterns[0]);
  return user_wants_thing(argc, argv, help_patterns, hp_size);
}

bool user_wants_recurse(int argc, char **argv) {
  const char *recurse_patterns[] = {"recurse", "-recurse", "--recurse", "-r"};
  const size_t rp_size = sizeof(recurse_patterns) / sizeof(recurse_patterns[0]);
  return user_wants_thing(argc, argv, recurse_patterns, rp_size);
}

bool user_wants_nodefer(int argc, char **argv) {
  const char *nodefer_patterns[] = {"nodefer", "-nodefer", "--nodefer", "-n"};
  const size_t np_size = sizeof(nodefer_patterns) / sizeof(nodefer_patterns[0]);
  return user_wants_thing(argc, argv, nodefer_patterns, np_size);
}

bool user_wants_static_check(int argc, char **argv) {
  const char *static_check_patterns[] = {"static", "-static", "--static", "-s"};
  const size_t sp_size = sizeof(static_check_patterns) / sizeof(static_check_patterns[0]);
  return user_wants_thing(argc, argv, static_check_patterns, sp_size);
}

void print_help() {
  static const char msg[] = "Usage: deadlock [help|nodefer|recurse|static]\n";
  write(1, msg, sizeof(msg));
}

int main(int argc, char **argv) {
  if (user_needs_help(argc, argv)) {
    print_help();
    return 0;
  }

  // The user doesn't need help, so we install the handler at least
  // use sigaction so we can optionally set SA_NODEFER
  struct sigaction sa;
  sa.sa_handler = segfault_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  if (user_wants_nodefer(argc, argv)) {
    sa.sa_flags |= SA_NODEFER;
  }
  sigaction(SIGSEGV, &sa, NULL);

  // But does the user want _more_ segfaults?
  if (user_wants_recurse(argc, argv)) {
    segfault_in_segfault = true;
  }

  // Does the user want explicit recursion protection?
  if (user_wants_static_check(argc, argv)) {
    use_static_check = true;
  }

  // All right, let's see what happens.
  do_segfault();
  return 0;
}
