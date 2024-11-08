#define _GNU_SOURCE
#include <ucontext.h>
#include <seccomp.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/audit.h>
#include <signal.h>
#include <sys/syscall.h>

void sigsys_handler(int signum, siginfo_t *info, void *context) {
    char *sc_name = seccomp_syscall_resolve_num_arch(info->si_arch, info->si_syscall);
    printf("Blocked syscall: %s\n", sc_name);
    printf("Error code: %d\n", info->si_code);

    // Get the context and print some details
    ucontext_t *ucontext = (ucontext_t *)context;
    printf("RIP: %p\n", (void *)ucontext->uc_mcontext.gregs[REG_RIP]);
    fflush(stdout);
}

// This sets up seccomp to block syscalls with `SECCOMP_RET_TRAP`, allowing us to observe them.
void setup_seccomp() {
    struct sock_filter filter[] = {
        // I'm not 100% sure what this does.  Loads the syscall number, I guess?  Right.
        { BPF_LD | BPF_W | BPF_ABS, 0, 0, offsetof(struct seccomp_data, nr) },

        // Allow write syscall
        { BPF_JMP | BPF_JEQ | BPF_K, 0, 1, __NR_write },
        { BPF_RET | BPF_K, 0, 0, SECCOMP_RET_ALLOW },

        // Trap (SIGSYS) on any other syscall
        { BPF_RET | BPF_K, 0, 0, SECCOMP_RET_TRAP },
    };

    struct sock_fprog prog = {
        .len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
        .filter = filter,
    };

    // Set up no_new_privs so seccomp rules apply to all child processes
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
        perror("prctl(NO_NEW_PRIVS)");
        exit(EXIT_FAILURE);
    }

    // Load the seccomp filter
    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog)) {
        perror("prctl(SECCOMP)");
        exit(EXIT_FAILURE);
    }
}

int main() {
    // Set up the SIGSYS signal handler
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = sigsys_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSYS, &sa, NULL) == -1) {
        perror("sigaction(SIGSYS)");
        exit(EXIT_FAILURE);
    }

    printf("Setting up seccomp filter, isn't this exciting?\n");
    setup_seccomp();
    printf("Seccomp filter is set up. Only 'write' and 'exit' syscalls are allowed. Others will trigger SIGSYS.\n");

    // This should work since 'write' is allowed
    write(STDOUT_FILENO, "Hello, world!\n", 14);

    // This should trigger SIGSYS because 'read' is not allowed
    char buffer[10];
    read(STDIN_FILENO, buffer, sizeof(buffer));
    printf("This line should never print.\n");

    return 0;
}

