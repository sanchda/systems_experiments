#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>


int seq = 0;
int seq2 = 0;

// Signal handler for SIGALRM
void sigalrm_handler(int signum) {
    // Attempt to allocate memory inside the signal handler
        if (seq2++ % 1000 == 0) {
            printf("Signal loop: Attempting to allocate memory %d\n", seq2);
        }
    char *buffer = (char *)malloc(4096);
    if (buffer != NULL) {
        free(buffer);
    }
}

int main() {
    struct sigaction sa;
    struct itimerval timer;

    // Setup the SIGALRM handler
    sa.sa_handler = sigalrm_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        perror("Error setting up signal handler");
        return 1;
    }

    // Configure the timer to trigger SIGALRM quickly and repeatedly
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 4;     // First alarm after 1 microsecond
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 4;  // Repeat alarm every 1 microsecond

    if (setitimer(ITIMER_REAL, &timer, NULL) == -1) {
        perror("Error setting up timer");
        return 1;
    }

    // Main loop: Continuously attempt to allocate memory
    while (1) {
        if (seq++ % 1000 == 0) {
            printf("Main loop: Attempting to allocate memory %d\n", seq);
        }
        char *buffer = (char *)malloc(4096);
        if (buffer != NULL) {
            buffer[0] = 'b'; // Do something with allocated memory
        }
        free(buffer);
    }

    return 0;
}

