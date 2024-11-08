#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

struct KillArgs {
  pid_t pid;
  pid_t tid;
  char type;
};

// Some globals
int extra_flags = 0;
char crash_type = 'K';
bool fastexit = false;
char signal_crash_type = 'K';
bool pause_in_handler = false;
bool skip_reraise = false;

// syscall wrappers
int tgkill(int tgid, int tid, int sig) {
  return syscall(__NR_tgkill, tgid, tid, sig);
}

pid_t gettid() { return syscall(SYS_gettid); }

// Do a segfault
void do_segfault(char type, pid_t pid, pid_t tid) {
  switch (type) {
  case 'K':
    printf("[%d:%d] SIGSEGV: kill %d\n", getpid(), gettid(), pid);
    fflush(stdout);
    kill(pid, SIGSEGV);
    break;
  case 'T':
    printf("[%d:%d] SIGSEGV: tgkill %d\n", getpid(), gettid(), tid);
    fflush(stdout);
    tgkill(pid, tid, SIGSEGV);
    break;
  case 'S': {
    printf("[%d:%d] SIGSEGV: segfault\n", getpid(), gettid());
    fflush(stdout);
    int *p = NULL;
    *p = 0;
  } break;
  default:
    printf("[%d:%d] Unknown type: '%c'\n", getpid(), gettid(), type);
    fflush(stdout);
    break;
  }
}

void pretend_to_be_in_container() {
  if (unshare(CLONE_NEWPID) == -1) {
    perror("unshare");
    exit(EXIT_FAILURE);
  }

  pid_t pid = fork();
  if (pid == -1) {
    perror("fork");
    exit(EXIT_FAILURE);
  } else if (pid == 0) {
    // Child process
    // Does nothing and returns
  } else {
    // Parent process
    // In old namespace, exits
    _exit(EXIT_SUCCESS);
  }
}

// Setup a sigaltstack
void setup_sigaltstack() {
  size_t sz = 4096 * 16;
  stack_t ss;
  ss.ss_sp = malloc(sz);
  ss.ss_size = sz;
  ss.ss_flags = 0;
  if (sigaltstack(&ss, NULL) == -1) {
    perror("sigaltstack");
    exit(EXIT_FAILURE);
  }
}

struct sigaction old_sa;
void segfault_handler(int signum) {
  // Double-check that this is signal 11
  if (signum != SIGSEGV) {
    printf("[%d:%d] Received wrong signal %d\n", getpid(), gettid(), signum);
    fflush(stdout);
    return;
  }

  if (pause_in_handler) {
    printf("[%d:%d] Pausing in the handler\n", getpid(), gettid());
    fflush(stdout);
    sleep(5);
  }
  printf("[%d:%d] Attempting to complete handler\n", getpid(), gettid());
  fflush(stdout);

  // Restore the SIG_DFL and re-raise the signal however the user wants
  if (!skip_reraise) {
    sigaction(SIGSEGV, &old_sa, NULL);
    pid_t pid = getpid();
    pid_t tid = gettid();
    do_segfault(signal_crash_type, pid, tid);
  }
}

// This thread just sits in a loop and prints "hello" every 1 second
void *bg_thread(void *arg) {
  // Just sleep for 5 seconds and die
  char *msg = (char *)arg;
  for (int i = 0; i < 5; i++) {
    printf("[%d:%d:%s] hello\n", getpid(), gettid(), msg);
    fflush(stdout);
    sleep(1);
  }
  printf("[%d:%d:%s] goodbye\n", getpid(), gettid(), msg);
  fflush(stdout);
  return NULL;
}

void *kill_thread(void *arg) {
  struct KillArgs *ka = (struct KillArgs *)arg;
  do_segfault(ka->type, ka->pid, ka->tid);

  printf("[%d:%d] I raised a segfault, but I'm alive now.\n", getpid(),
         gettid());
  fflush(stdout);
  return NULL;
}

// Sorry, but I like it this way.
// clang-format off
void action_signal_tgkill() { signal_crash_type = 'T'; }
void action_signal_kill() { signal_crash_type = 'K'; }
void action_signal_segfault() { signal_crash_type = 'S'; }
void action_do_kill() { crash_type = 'K'; }
void action_do_tgkill() { crash_type = 'T'; }
void action_do_segfault() { crash_type = 'S'; }
void action_container() { pretend_to_be_in_container(); }
void action_altstack() { setup_sigaltstack(); extra_flags |= SA_ONSTACK; }
void action_thread() { pthread_t thread; pthread_create(&thread, NULL, bg_thread, "bg_thread"); }
void action_pause() { pause_in_handler = true; }
void action_fastexit() { fastexit = true; }
void action_noraise() { skip_reraise = true; }

#define COMMANDS \
    X("signal_tgkill",    "Use tgkill in signal handler",            "Using tgkill in signal handler",       action_signal_tgkill) \
    X("signal_kill",      "Use kill in signal handler",              "Using kill in signal handler",         action_signal_kill) \
    X("signal_segfault",  "Use segfault in signal handler",          "Using segfault in signal handler",     action_signal_segfault) \
    X("do_kill",          "Emit a kill signal",                      "Emitting a kill signal",               action_do_kill) \
    X("do_tgkill",        "Emit a tgkill signal",                    "Emitting a tgkill signal",             action_do_tgkill) \
    X("do_segfault",      "Emit a segfault",                         "Emitting a segfault",                  action_do_segfault) \
    X("container",        "Pretend to be in a container",            "Pretending to be in a container",      action_container) \
    X("altstack",         "Use an alternate stack",                  "Using an alternate stack",             action_altstack) \
    X("thread",           "Run a thread in the background",          "Running a thread in background",       action_thread) \
    X("pause",            "Pause in the signal handler",             "Pausing in the signal handler",        action_pause) \
    X("fastexit",         "Exit ASAP rather than sitting in a loop", "Exiting ASAP",                         action_fastexit) \
    X("noraise",          "Do not re-raise the signal",              "Not re-raising the signal",            action_noraise)
// clang-format on

void process_command(const char *command) {
#define X(cmd, help, msg, action)                                              \
  if (strcmp(command, cmd) == 0) {                                             \
    printf("> %s\n", msg);                                                     \
    action();                                                                  \
    return;                                                                    \
  }
  COMMANDS
#undef X

  // If command is not found, print unknown command message
  printf("<<<< Unknown argument: %s\n", command);
}

void print_help(char *arg0) {
  printf("Usage: %s [options]\n", arg0);
  printf("Options:\n");
#define X(cmd, help, msg, action) printf("    %-15s - %s\n", cmd, help);
  COMMANDS
#undef X
}

int main(int argc, char **argv) {
  // Hey, let's scan for a help request first.  Just, you know.  Because.
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "help") == 0) {
      print_help(argv[0]);
      return 0;
    }
  }

  // Process the commands
  for (int i = 1; i < argc; i++) {
    process_command(argv[i]);
  }

  // Set up the signal handler for SIGSEGV
  struct sigaction sa;
  sa.sa_handler = segfault_handler;
  sa.sa_flags = SA_NODEFER | SA_SIGINFO | extra_flags;
  sigemptyset(&sa.sa_mask);

  if (sigaction(SIGSEGV, &sa, &old_sa) == -1) {
    perror("sigaction");
    exit(EXIT_FAILURE);
  }
  old_sa.sa_flags = 0; // The base case I'm tracking has 0 for the old handler,
                       // so we keep that

  // Mediate the kill in a separate thread
  struct KillArgs ka = {.pid = getpid(), .tid = gettid(), .type = crash_type};
  pthread_t kill_thread_id;
  pthread_create(&kill_thread_id, NULL, kill_thread, &ka);

  // If the user wants to exit, then exit, else sit in a loop
  if (!fastexit) {
    bg_thread("main");
  }

  return 0;
}
