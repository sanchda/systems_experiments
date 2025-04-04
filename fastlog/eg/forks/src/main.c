#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "libmmlog.h"

#define NUM_PROCESSES 8
#define MESSAGES_PER_PROCESS 1000
#define MAX_MESSAGE_SIZE 512
#define CHUNK_SIZE (4 * 4096)  // 16KB chunks
#define NUM_CHUNKS 16

// Random message generation
static const char* word_list[] = {
    "atomic", "memory", "mapped", "log", "concurrent", "process", "thread",
    "synchronization", "lock", "append", "only", "ftruncate", "mmap", "file",
    "metadata", "chunk", "buffer", "ring", "head", "tail", "write", "read"
};
#define NUM_WORDS (sizeof(word_list) / sizeof(word_list[0]))

// Generate a random message
void generate_random_message(char* buffer, size_t max_size) {
    int word_count = 3 + rand() % 8;  // 3 to 10 words
    size_t pos = 0;

    // Process ID and timestamp prefix for identification
    pos += snprintf(buffer + pos, max_size - pos, "[PID %d] [%ld] ", getpid(), time(NULL));

    for (int i = 0; i < word_count && pos < max_size - 1; i++) {
        const char* word = word_list[rand() % NUM_WORDS];
        pos += snprintf(buffer + pos, max_size - pos, "%s ", word);
    }

    // Ensure null termination
    buffer[pos] = '\0';
}

// Entry point for child processes
void child_process(const char* log_filename, int process_num) {
    // Seed random number generator differently for each process
    srand(time(NULL) + getpid());

    // Open the log
    log_handle_t* handle = mmlog_open(log_filename, CHUNK_SIZE, NUM_CHUNKS);
    if (!handle) {
        fprintf(stderr, "Process %d: Failed to open log: %s\n", process_num, strerror(errno));
        exit(1);
    }

    // Write messages
    char message[MAX_MESSAGE_SIZE];
    for (int i = 0; i < MESSAGES_PER_PROCESS; i++) {
        generate_random_message(message, sizeof(message));
        size_t msg_len = strlen(message);

        // Add a newline for readability in validation
        message[msg_len] = '\n';
        msg_len++;
        message[msg_len] = '\0';

        if (!mmlog_insert(handle, message, msg_len)) {
            fprintf(stderr, "Process %d: Failed to insert message %d: %s\n",
                    process_num, i, strerror(errno));
            free(handle);
            exit(1);
        }

        // Add some random delays to increase contention chances
        if (rand() % 10 == 0) {
            usleep(rand() % 1000);  // 0-1ms delay
        }
    }

    // Clean up
    free(handle);
    exit(0);
}

// Validate log file contents
bool validate_log(const char* log_filename, int expected_total_messages) {
    // Read the entire log file
    FILE* file = fopen(log_filename, "rb");
    if (!file) {
        perror("Failed to open log file for validation");
        return false;
    }

    // Count lines (each message ends with a newline)
    int line_count = 0;
    char buffer[MAX_MESSAGE_SIZE];

    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        line_count++;

        // Check if each line has [PID xx] format
        if (strstr(buffer, "[PID ") == NULL) {
            fprintf(stderr, "Invalid log line: %s", buffer);
        }
    }

    fclose(file);

    printf("Validation: Found %d messages, expected %d\n",
           line_count, expected_total_messages);

    return line_count == expected_total_messages;
}

int main() {
    const char* log_filename = "fork_test.log";
    pid_t child_pids[NUM_PROCESSES];

    printf("Starting fork-based mmlog test with %d processes\n", NUM_PROCESSES);
    printf("Each process will write %d messages\n", MESSAGES_PER_PROCESS);

    // Remove old log file if it exists
    unlink(log_filename);

    // Fork child processes
    for (int i = 0; i < NUM_PROCESSES; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("Fork failed");
            exit(1);
        } else if (pid == 0) {
            // Child process
            child_process(log_filename, i);
            // Child never reaches here
        } else {
            // Parent process
            child_pids[i] = pid;
            printf("Started child process %d with PID %d\n", i, pid);
        }
    }

    // Wait for all children to complete
    int status;
    int success_count = 0;

    for (int i = 0; i < NUM_PROCESSES; i++) {
        waitpid(child_pids[i], &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            printf("Child process %d (PID %d) completed successfully\n", i, child_pids[i]);
            success_count++;
        } else {
            printf("Child process %d (PID %d) failed with status %d\n",
                   i, child_pids[i], WEXITSTATUS(status));
        }
    }

    printf("\n%d/%d processes completed successfully\n", success_count, NUM_PROCESSES);

    // Validate the log
    log_handle_t* handle = mmlog_open(log_filename, CHUNK_SIZE, NUM_CHUNKS);
    if (!handle) {
        fprintf(stderr, "Failed to open log for validation: %s\n", strerror(errno));
        return 1;
    }
    mmlog_trim(handle);

    bool validation_result = validate_log(log_filename, NUM_PROCESSES * MESSAGES_PER_PROCESS);

    printf("\nTest %s!\n", validation_result ? "PASSED" : "FAILED");

    return validation_result ? 0 : 1;
}
