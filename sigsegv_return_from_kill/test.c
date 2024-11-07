#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <pthread.h>

// Stores the old sigaction
struct sigaction old_sa;

// tgkill calling the syscall directly
int tgkill(int tgid, int tid, int sig) {
    return syscall(__NR_tgkill, tgid, tid, sig);
}

// Setup a sigaltstack
void setup_sigaltstack()
{
    size_t sz = 4096*16;
    stack_t ss;
    ss.ss_sp = malloc(sz);
    ss.ss_size = sz;
    ss.ss_flags = 0;
    if (sigaltstack(&ss, NULL) == -1) {
        perror("sigaltstack");
        exit(EXIT_FAILURE);
    }
}

void segfault_handler(int signum) {
    printf("Attempting to return from the handler...\n");
    sigaction(SIGSEGV, &old_sa, NULL);
    pid_t pid = getpid();
    pid_t tid = syscall(SYS_gettid);
    tgkill(pid, tid, SIGSEGV);
}

// This thread just sits in a loop and prints "hello" every 1 second
void* thread_func(void* arg) {
    while (1) {
        printf("hello\n");
        sleep(1);
    }
    return NULL;
}

struct KillArgs {
  pid_t pid;
  pid_t tid;
};

void* tgkill_thread(void* arg) {
  printf("Raising SIGSEGV...\n"); fflush(stdout);
  struct KillArgs* ka = (struct KillArgs*)arg;
  tgkill(ka->pid, ka->tid, SIGSEGV);
  return NULL;
}

void *kill_thread(void *arg) {
  struct KillArgs* ka = (struct KillArgs*)arg;
  kill(ka->pid, SIGSEGV);
  return NULL;
}

int main(int argc, char** argv) {
    // Look for keywords in the command line arguments
    int use_tgkill = false;
    int use_altstack = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "tgkill") == 0) {
            printf("---- Using tgkill instead of kill\n");
            use_tgkill = true;
        }
        else if (strcmp(argv[i], "altstack") == 0) {
            printf("---- Using an alternate stack\n");
            use_altstack = true;
        }
        else {
            printf("Unknown argument: %s\n", argv[i]);
        }
    }

    // Setup the altstack
    int extra_flags = 0;
    if (use_altstack) {
      extra_flags = SA_ONSTACK;
      setup_sigaltstack();
    }

    // Launch a thread
    pthread_t thread;
    pthread_create(&thread, NULL, thread_func, NULL);

    // Set up the signal handler for SIGSEGV
    struct sigaction sa;
    sa.sa_handler = segfault_handler;
    sa.sa_flags = SA_NODEFER|SA_SIGINFO|extra_flags;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGSEGV, &sa, &old_sa) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    old_sa.sa_flags = 0; // Not sure why, but this is the base case

    // Mediate the kill in a separate thread
    struct KillArgs ka = {.pid = getpid(), .tid = syscall(SYS_gettid)};

    pthread_t kill_thread_id;
    pthread_create(&kill_thread_id, NULL, kill_thread, &ka);

    while (1) {
        printf("main thread\n");
        sleep(1);
    }

    printf("If you see this, the handler returned (but this is undefined behavior!)\n");
    return 0;
}

