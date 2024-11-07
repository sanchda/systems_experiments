#include <stdio.h>
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

int main() {
    // Setup the altstack
    setup_sigaltstack();

    // Launch a thread
    pthread_t thread;
    pthread_create(&thread, NULL, thread_func, NULL);

    // Set up the signal handler for SIGSEGV
    struct sigaction sa;
    sa.sa_handler = segfault_handler;
    sa.sa_flags = SA_ONSTACK|SA_NODEFER|SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGSEGV, &sa, &old_sa) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    // Not sure why, but in the scenario I'm tracking the sa_flags for the old handler are 0
    old_sa.sa_flags = 0;

    printf("Raising SIGSEGV...\n");
    kill(getpid(), SIGSEGV);

    printf("If you see this, the handler returned (but this is undefined behavior!)\n");
    return 0;
}

