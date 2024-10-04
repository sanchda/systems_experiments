#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <sys/wait.h>

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

pid_t spawn_timebomb_child(int seconds) {
    pid_t pid = fork();
    switch (pid) {
        case -1:
            perror("fork");
            break;
        case 0:
            sleep(seconds);
            printf("PID %d terminating !\n", getpid());
            _exit(EXIT_SUCCESS);
            break;
        default:
            break;
    }
}

pid_t spawn_evil_child() {
    pid_t pid = fork();
    pid_t grandchild;
    switch (pid) {
        case -1:
            perror("fork");
            break;
        case 0:
            grandchild = spawn_timebomb_child(60);
            printf("Spawned grandchild with PID %d\n", grandchild);
            _exit(EXIT_SUCCESS);
            break;
        default:
            waitpid(pid, NULL, 0);
            break;
    }
}

int main() {
    pretend_to_be_in_container();
    printf("My PID is now %d\n", getpid());

    // Spawn some regular, well-behaved children who eat their vegetables and go to bed when asked.
    for (int i = 0; i < 5; i++) {
        pid_t child = spawn_timebomb_child(5);
        printf("Spawned child with PID %d\n", child);
    }

    // Spawn an evil child who squashes frogs and glares at the mail carrier
    spawn_evil_child();

    // waitall loop, wait on all children until we get an ECHILD
    int status;
    pid_t child;

    while (1) {
        pid_t result = waitpid(-1, &status, 0);
        if (result > 0) {
            printf("Child (PID %d) exited with status %d\n", result, status);
        } else if (result == -1) {
            if (errno == ECHILD) {
                printf("No more children to wait for\n");
                break;
            } else {
                perror("waitpid");
            }
        }
    }

    return 0;
}

