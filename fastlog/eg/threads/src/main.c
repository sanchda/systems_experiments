#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdatomic.h>  // Added for atomic operations

#include "libmmlog.h"

#define NUM_THREADS 16
#define MESSAGES_PER_THREAD 500
#define MAX_MESSAGE_SIZE 512
#define CHUNK_SIZE (4 * 4096)  // 16KB chunks
#define NUM_CHUNKS 16

// Global atomic sequence counter
atomic_int global_seq_counter = 0;
atomic_int global_msg_counter = 0;

// Random message generation
static const char* word_list[] = {
    "atomic", "memory", "mapped", "log", "concurrent", "process", "thread",
    "synchronization", "lock", "append", "only", "ftruncate", "mmap", "file",
    "metadata", "chunk", "buffer", "ring", "head", "tail", "write", "read",
    "checkout", "variable", "mutex", "condition", "barrier", "atomic", "signal",
    "wait", "notify", "broadcast", "acquire", "release", "yield", "sleep", "join"
};
#define NUM_WORDS (sizeof(word_list) / sizeof(word_list[0]))

typedef struct {
    const char* log_filename;
    int thread_num;
    int messages_written;
    bool success;
} thread_args_t;

// Worker thread information for the stress test
typedef struct {
    pthread_t thread;
    thread_args_t args;
} worker_info_t;

// Generate a random message
size_t generate_random_message(char* buffer, size_t max_size, int seq_num, int thread_id) {
    int word_count = 3 + rand() % 8;  // 3 to 10 words
    size_t pos = 0;

    // Thread ID and timestamp prefix for identification
    pos += snprintf(buffer + pos, max_size - pos, " [SEQ %d] [THR %d] [%ld] ", seq_num, thread_id, time(NULL));

    for (int i = 0; i < word_count && pos < max_size - 1; i++) {
        const char* word = word_list[rand() % NUM_WORDS];
        pos += snprintf(buffer + pos, max_size - pos, "%s ", word);
    }

    // Ensure null termination
    buffer[pos] = '\n';
    pos++;
    buffer[pos] = '\0';
    return pos;
}

// Thread worker function
void* thread_worker(void* arg) {
    thread_args_t* args = (thread_args_t*)arg;

    // Use thread ID to seed the random number generator
    unsigned int thread_seed = (unsigned int)time(NULL) ^ (args->thread_num << 16);
    srand(thread_seed);

    // Open the log; strictly speaking it is not recommended or necessary to open the log at the thread level,
    // but this *should* be allowed and everything should work correctly.  The only consequence is that we'll
    // duplicate all of our accounting overhead.
    log_handle_t* handle = mmlog_open(args->log_filename, CHUNK_SIZE, NUM_CHUNKS);
    if (!handle) {
        fprintf(stderr, "Thread %d: Failed to open log: %s\n", args->thread_num, mmlog_strerror_cur());
        args->success = false;
        return NULL;
    }

    // Write messages
    args->messages_written = 0;
    char message[MAX_MESSAGE_SIZE];

    for (int i = 0; i < MESSAGES_PER_THREAD; i++) {
        int seq_num = atomic_fetch_add(&global_seq_counter, 1);
        size_t msg_len = generate_random_message(message, sizeof(message), seq_num, args->thread_num);

        if (!mmlog_insert(handle, message, msg_len)) {
            fprintf(stderr, "Thread %d: Failed to insert message %d: %s\n",
                    args->thread_num, i, mmlog_strerror_cur());
            free(handle);
            args->success = false;
            return NULL;
        }
        atomic_fetch_add(&global_seq_counter, 1);
        args->messages_written++;
    }

    // Clean up
    mmlog_trim(handle);
    free(handle);
    args->success = true;
    return NULL;
}

// Validate log file contents with thread-specific checks
bool validate_log(const char* log_filename, int expected_total_messages) {
    // Read the entire log file
    FILE* file = fopen(log_filename, "rb");
    if (!file) {
        perror("Failed to open log file for validation");
        return false;
    }

    // Count lines and track messages per thread
    int line_count = 0;
    int thread_message_counts[NUM_THREADS] = {0};
    char buffer[MAX_MESSAGE_SIZE];

    // Track sequence numbers for validation
    bool* seen_sequences = calloc(expected_total_messages, sizeof(bool));
    if (!seen_sequences) {
        perror("Failed to allocate sequence tracking array");
        fclose(file);
        return false;
    }

    int duplicate_sequences = 0;
    int missing_sequences = 0;
    int max_sequence = -1;

    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        line_count++;

        // Extract thread ID from message format [THR xx]
        char* thread_marker = strstr(buffer, "[THR ");
        if (thread_marker) {
            int thread_id;
            if (sscanf(thread_marker, "[THR %d]", &thread_id) == 1) {
                if (thread_id >= 0 && thread_id < NUM_THREADS) {
                    thread_message_counts[thread_id]++;
                } else {
                    fprintf(stderr, "Invalid thread ID in log: %d\n", thread_id);
                }
            }
        } else {
            fprintf(stderr, "Invalid log line format: %s", buffer);
        }

        // Extract and validate sequence number
        char* seq_marker = strstr(buffer, "[SEQ ");
        if (seq_marker) {
            int seq_num;
            if (sscanf(seq_marker, "[SEQ %d]", &seq_num) == 1) {
                if (seq_num >= 0 && seq_num < global_msg_counter) {
                    if (seen_sequences[seq_num]) {
                        fprintf(stderr, "Duplicate sequence number: %d\n", seq_num);
                        duplicate_sequences++;
                    }
                    seen_sequences[seq_num] = true;
                    if (seq_num > max_sequence) {
                        max_sequence = seq_num;
                    }
                } else {
                    // fprintf(stderr, "Sequence number out of range: %d/%d\n", seq_num, global_msg_counter);
                }
            }
        }
    }

    fclose(file);

    // Check for missing sequence numbers
    for (int i = 0; i <= max_sequence; i++) {
        if (!seen_sequences[i]) {
            missing_sequences++;
        }
    }

    printf("Validation: Found %d messages, expected %d\n",
           line_count, expected_total_messages);
    printf("Sequence validation: max=%d, duplicates=%d, missing=%d\n",
           max_sequence, duplicate_sequences, missing_sequences);

    // Print per-thread message counts
    printf("Messages per thread:\n");
    for (int i = 0; i < NUM_THREADS; i++) {
        printf("Thread %2d: %4d messages\n", i, thread_message_counts[i]);

        if (thread_message_counts[i] != MESSAGES_PER_THREAD) {
            fprintf(stderr, "Thread %d: Expected %d messages but found %d\n",
                    i, MESSAGES_PER_THREAD, thread_message_counts[i]);
        }
    }

    free(seen_sequences);
    return line_count == expected_total_messages &&
           duplicate_sequences == 0 &&
           missing_sequences == 0;
}

// Additional stress test with varying message sizes
void run_mixed_size_test(const char* log_filename) {
    printf("\nRunning mixed message size test...\n");

    // Remove old log file if it exists
    unlink(log_filename);

    // Reset the sequence counter for this test
    atomic_store(&global_seq_counter, 0);

    // Open the log
    log_handle_t* handle = mmlog_open(log_filename, CHUNK_SIZE, NUM_CHUNKS);
    if (!handle) {
        fprintf(stderr, "Failed to open log for mixed size test: %s\n", mmlog_strerror_cur());
        return;
    }

    // Insert a variety of message sizes
    const int num_sizes = 5;
    size_t sizes[] = {16, 128, 512, 2048, 8192};  // Various sizes including cross-chunk
    int count_per_size = 50;

    for (int i = 0; i < num_sizes; i++) {
        size_t size = sizes[i];
        char* buffer = malloc(size);
        if (!buffer) {
            fprintf(stderr, "Failed to allocate buffer of size %lud\n", size);
            continue;
        }

        // Fill buffer with pattern that includes the size
        snprintf(buffer, size, "[SIZE %lud] ", size);
        memset(buffer + strlen(buffer), 'A' + i, size - strlen(buffer) - 1);
        buffer[size - 1] = '\0';

        printf("Writing %d messages of size %lud bytes\n", count_per_size, size);

        for (int j = 0; j < count_per_size; j++) {
            // Add atomic sequence number at the end
            int seq_num = atomic_fetch_add(&global_seq_counter, 1);
            char seq_suffix[32];
            snprintf(seq_suffix, sizeof(seq_suffix), " [SEQ %d]", seq_num);
            size_t seq_len = strlen(seq_suffix);

            if (strlen(buffer) + seq_len < size) {
                strcpy(buffer + strlen(buffer) - seq_len, seq_suffix);
            }

            if (!mmlog_insert(handle, buffer, size)) {
                fprintf(stderr, "Failed to insert message of size %lud: %s\n",
                        size, mmlog_strerror_cur());
                break;
            }
        }

        free(buffer);
    }

    // Clean up
    mmlog_trim(handle);
    free(handle);

    // Validation - just count the total bytes
    struct stat st;
    if (stat(log_filename, &st) == 0) {
        int expected_size = 0;
        for (int i = 0; i < num_sizes; i++) {
            expected_size += sizes[i] * count_per_size;
        }

        printf("Mixed size test: File size is %ld bytes (expected approximately %d bytes)\n",
               st.st_size, expected_size);
    } else {
        perror("Failed to stat log file");
    }
}

int main(void) {
    const char* log_filename = "thread_test.log";
    worker_info_t workers[NUM_THREADS];

    printf("Starting thread-based mmlog test with %d threads\n", NUM_THREADS);
    printf("Each thread will write %d messages\n", MESSAGES_PER_THREAD);

    // Remove old log file if it exists
    unlink(log_filename);

    // Initialize atomic sequence counter
    atomic_store(&global_seq_counter, 0);

    // Create and start worker threads
    for (int i = 0; i < NUM_THREADS; i++) {
        workers[i].args.log_filename = log_filename;
        workers[i].args.thread_num = i;
        workers[i].args.messages_written = 0;
        workers[i].args.success = false;

        int result = pthread_create(&workers[i].thread, NULL, thread_worker, &workers[i].args);
        if (result != 0) {
            fprintf(stderr, "Failed to create thread %d: %s\n", i, strerror(result));
            exit(1);
        }

        printf("Started thread %d\n", i);
    }

    // Wait for all threads to complete
    int success_count = 0;
    int total_messages = 0;

    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(workers[i].thread, NULL);

        if (workers[i].args.success) {
            printf("Thread %d completed successfully, wrote %d messages\n",
                   i, workers[i].args.messages_written);
            success_count++;
            total_messages += workers[i].args.messages_written;
        } else {
            printf("Thread %d failed, wrote only %d messages\n",
                   i, workers[i].args.messages_written);
        }
    }

    printf("\n%d/%d threads completed successfully\n", success_count, NUM_THREADS);
    printf("Total messages written: %d\n", total_messages);

    // Since we're done, trim the file
    log_handle_t *handle = mmlog_open(log_filename, CHUNK_SIZE, NUM_CHUNKS);
    mmlog_trim(handle);

    // Validate the log
    bool validation_result = validate_log(log_filename, total_messages);

    printf("\nThreaded test %s!\n", validation_result ? "PASSED" : "FAILED");

    // Run the mixed size test
    run_mixed_size_test("mixed_size_test.log");

    return validation_result ? 0 : 1;
}
