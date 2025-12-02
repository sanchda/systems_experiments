#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>

static pid_t child_pid = 0;

void signal_pass_handler(int signo) {
    if (child_pid > 0) {
        kill(child_pid, signo);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <--ignore|--no-ignore|--pass|--default> <binary_path>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *mode = argv[1];
    const char *binary = argv[2];

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        execl(binary, binary, NULL);
        perror("execl");
        _exit(EXIT_FAILURE);
    }
    child_pid = pid;

    // Setup signal handling based on mode
    if (strcmp(mode, "--default") == 0) {
        /* Do nothing - leave signals in default state */
    } else {
        struct sigaction sa = {0};

        if (strcmp(mode, "--ignore") == 0) {
            sa.sa_handler = SIG_IGN;
        } else if (strcmp(mode, "--no-ignore") == 0) {
            sa.sa_handler = SIG_DFL;
        } else if (strcmp(mode, "--pass") == 0) {
            sa.sa_handler = signal_pass_handler;
        } else {
            fprintf(stderr, "Invalid mode: %s\n", mode);
            kill(pid, SIGTERM);
            return EXIT_FAILURE;
        }

        sigaction(SIGTERM, &sa, NULL);
        sigaction(SIGINT, &sa, NULL);
    }

    printf("Wrapper PID: %d, Child PID: %d, Mode: %s\n", getpid(), pid, mode);
    printf("Send signal to wrapper: kill -INT %d  (or kill -TERM %d)\n", getpid(), getpid());
    fflush(stdout);

    // Die after 10s, but wait in the meantime
    for (int i = 0; i < 10; i++) {
        int status;
        pid_t result = waitpid(pid, &status, WNOHANG);

        if (result == pid) {
            printf("Child exited\n");
            return WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;
        }

        sleep(1);
    }

    printf("Timeout after 10 seconds\n");
    return EXIT_SUCCESS;
}
