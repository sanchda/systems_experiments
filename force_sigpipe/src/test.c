#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

void sigpipe_handler(int sig) {
    printf("Caught SIGPIPE signal %d\n", sig);
}

int main() {
    int pipefd[2];

    // Install SIGPIPE handler
    struct sigaction sa = {.sa_handler = sigpipe_handler};
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGPIPE, &sa, NULL) == -1) {
        perror("Failed to install SIGPIPE handler");
        return EXIT_FAILURE;
    }

    // Create a pipe
    if (pipe(pipefd) == -1) {
        perror("Failed to create pipe");
        return EXIT_FAILURE;
    }

    // Close the read end
    if (close(pipefd[0]) == -1) {
        perror("Failed to close read end of pipe????");
        return EXIT_FAILURE;
    }

    // Try to write to the pipe with closed read end
    printf("I'm about to write to a closed pipe!  This is going to be bad!!\n");

    int bytes_written = write(pipefd[1], "test", 4);
    if (bytes_written == -1) {
        printf("Write failed: %s\n", strerror(errno));
    }
    return EXIT_SUCCESS;
}

