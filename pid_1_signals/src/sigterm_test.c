#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

volatile sig_atomic_t got_signal = 0;

void sigterm_handler(int _signo) {
    (void)_signo; // Unused parameter
    const char msg[] = "Signal received\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1); // printf is not async-signal-safe
    got_signal = 1;
}

int main(void) {
    struct sigaction sa = {0};
    sa.sa_handler = sigterm_handler;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    printf("PID %d waiting for SIGINT...\n", getpid());

    // Hot sleep; whatever
    while (!got_signal) {
        pause();
    }

    printf("Exiting cleanly\n");
    fflush(stdout);

    return EXIT_SUCCESS;
}
