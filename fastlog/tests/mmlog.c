#include <errno.h>
#include <pthread.h>
#include <sys/wait.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include "unity.h"
#include "mmlog.h"  // The header we're testing

// Test setup and teardown functions
void setUp(void) {
    // Setup code executed before each test
}

void tearDown(void) {
    // Cleanup code executed after each test
}

// Utility functions for testing
static const char* TEST_LOG_FILENAME = "test_mmlog.log";
static const uint32_t TEST_CHUNK_SIZE = 4096;  // Standard page size
static const uint32_t TEST_CHUNK_COUNT = 4;    // Number of chunks for tests

// Helper function to clean up test files
void cleanup_test_files(void) {
    unlink(TEST_LOG_FILENAME);
    char meta_filename[256];
    snprintf(meta_filename, sizeof(meta_filename), "%s.mmlog", TEST_LOG_FILENAME);
    unlink(meta_filename);
}

void test_mmlog_open_and_close(void) {
    cleanup_test_files();

    log_handle_t* handle = mmlog_open(TEST_LOG_FILENAME, TEST_CHUNK_SIZE, TEST_CHUNK_COUNT);
    TEST_ASSERT_NOT_NULL(handle);

    // Check that files were created
    char meta_filename[256];
    snprintf(meta_filename, sizeof(meta_filename), "%s.mmlog", TEST_LOG_FILENAME);

    struct stat st;
    TEST_ASSERT_EQUAL_INT(0, stat(TEST_LOG_FILENAME, &st));
    TEST_ASSERT_EQUAL_INT(0, stat(meta_filename, &st));

    // Test metadata initialization
    TEST_ASSERT_EQUAL_UINT32(MMLOG_VERSION, handle->metadata->version);
    TEST_ASSERT_EQUAL_UINT32(LOG_PAGE_SIZE, handle->metadata->page_size);
    TEST_ASSERT_EQUAL_UINT32(TEST_CHUNK_SIZE, handle->metadata->chunk_size);
    TEST_ASSERT_TRUE(atomic_load(&handle->metadata->is_ready));

    // Clean up
    free(handle->chunks.buffer);
    free(handle);

    cleanup_test_files();
}

void test_mmlog_insert_and_checkout(void) {
    cleanup_test_files();

    log_handle_t* handle = mmlog_open(TEST_LOG_FILENAME, TEST_CHUNK_SIZE, TEST_CHUNK_COUNT);
    TEST_ASSERT_NOT_NULL(handle);

    // Test data
    const char test_data[] = "Hello, mmlog!";
    size_t data_size = strlen(test_data) + 1;  // Include null terminator

    // Insert data into the log
    TEST_ASSERT_TRUE(mmlog_insert(handle, test_data, data_size));

    // Check cursor position
    TEST_ASSERT_EQUAL_UINT64(data_size, atomic_load(&handle->metadata->cursor));

    // Checkout a range to read back the data
    uint64_t cursor = 0;  // Start of the log
    chunk_info_t* chunk = mmlog_rb_checkout(handle, cursor);
    TEST_ASSERT_NOT_NULL(chunk);

    // Verify the data
    uint64_t offset = cursor - chunk->start_offset;
    char* read_data = (char*)chunk->mapping + offset;
    TEST_ASSERT_EQUAL_STRING(test_data, read_data);

    // Release the chunk
    atomic_fetch_sub(&chunk->ref_count, 1);

    // Clean up
    free(handle->chunks.buffer);
    free(handle);

    cleanup_test_files();
}

void test_mmlog_multiple_inserts(void) {
    cleanup_test_files();

    log_handle_t* handle = mmlog_open(TEST_LOG_FILENAME, TEST_CHUNK_SIZE, TEST_CHUNK_COUNT);
    TEST_ASSERT_NOT_NULL(handle);

    // Test multiple inserts
    const char* test_strings[] = {
        "First entry",
        "Second entry",
        "Third entry",
        "Fourth entry with some longer content to test larger inserts"
    };

    uint64_t offsets[4] = {0}; // Store offsets for verification

    // Insert all strings
    for (int i = 0; i < 4; i++) {
        size_t len = strlen(test_strings[i]) + 1;
        offsets[i] = atomic_load(&handle->metadata->cursor);
        TEST_ASSERT_TRUE(mmlog_insert(handle, test_strings[i], len));
    }

    // Verify each string
    for (int i = 0; i < 4; i++) {
        chunk_info_t* chunk = mmlog_rb_checkout(handle, offsets[i]);
        TEST_ASSERT_NOT_NULL(chunk);

        uint64_t offset = offsets[i] - chunk->start_offset;
        char* read_data = (char*)chunk->mapping + offset;
        TEST_ASSERT_EQUAL_STRING(test_strings[i], read_data);

        atomic_fetch_sub(&chunk->ref_count, 1);
    }

    // Clean up
    free(handle->chunks.buffer);
    free(handle);

    cleanup_test_files();
}

void test_mmlog_large_insert(void) {
    cleanup_test_files();

    log_handle_t* handle = mmlog_open(TEST_LOG_FILENAME, TEST_CHUNK_SIZE, TEST_CHUNK_COUNT);
    TEST_ASSERT_NOT_NULL(handle);

    // Create a large buffer that crosses chunk boundaries
    size_t large_size = TEST_CHUNK_SIZE * 1.5;  // 1.5 chunks
    char* large_buffer = (char*)malloc(large_size);
    TEST_ASSERT_NOT_NULL(large_buffer);

    // Fill with a pattern
    for (size_t i = 0; i < large_size; i++) {
        large_buffer[i] = (char)(i % 256);
    }

    // Insert the large buffer
    uint64_t offset = atomic_load(&handle->metadata->cursor);
    TEST_ASSERT_TRUE(mmlog_insert(handle, large_buffer, large_size));

    // Verify data integrity through multiple chunks
    uint64_t current_offset = offset;
    size_t bytes_verified = 0;

    while (bytes_verified < large_size) {
        chunk_info_t* chunk = mmlog_rb_checkout(handle, current_offset);
        TEST_ASSERT_NOT_NULL(chunk);

        uint64_t chunk_offset = current_offset - chunk->start_offset;
        size_t bytes_to_verify = large_size - bytes_verified;

        // Limit verification to current chunk
        if (chunk_offset + bytes_to_verify > chunk->size) {
            bytes_to_verify = chunk->size - chunk_offset;
        }

        // Compare data
        char* read_data = (char*)chunk->mapping + chunk_offset;
        for (size_t i = 0; i < bytes_to_verify; i++) {
            TEST_ASSERT_EQUAL_UINT8(large_buffer[bytes_verified + i], read_data[i]);
        }

        atomic_fetch_sub(&chunk->ref_count, 1);

        // Move to next chunk
        current_offset += bytes_to_verify;
        bytes_verified += bytes_to_verify;
    }

    free(large_buffer);
    free(handle->chunks.buffer);
    free(handle);

    cleanup_test_files();
}

typedef struct {
    log_handle_t* handle;
    int thread_id;
    int iterations;
    size_t data_size;
    char* results;  // For validation
} thread_test_args_t;

void* thread_insert_routine(void* arg) {
    thread_test_args_t* args = (thread_test_args_t*)arg;

    // Create unique test data for this thread
    char* test_data = (char*)malloc(args->data_size);
    if (!test_data) return NULL;

    // Generate a pattern with thread ID embedded
    for (size_t i = 0; i < args->data_size; i++) {
        test_data[i] = (char)((args->thread_id * 10) + (i % 10));
    }

    // Insert data in multiple iterations
    for (int i = 0; i < args->iterations; i++) {
        args->results[i] = mmlog_insert(args->handle, test_data, args->data_size);
    }

    free(test_data);
    return NULL;
}

void test_mmlog_concurrent_inserts(void) {
    cleanup_test_files();

    log_handle_t* handle = mmlog_open(TEST_LOG_FILENAME, TEST_CHUNK_SIZE, TEST_CHUNK_COUNT);
    TEST_ASSERT_NOT_NULL(handle);

    const int NUM_THREADS = 4;
    const int ITERATIONS = 10;
    const size_t DATA_SIZE = 100;

    pthread_t threads[NUM_THREADS];
    thread_test_args_t args[NUM_THREADS];

    // Initialize thread arguments
    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].handle = handle;
        args[i].thread_id = i;
        args[i].iterations = ITERATIONS;
        args[i].data_size = DATA_SIZE;
        args[i].results = (char*)calloc(ITERATIONS, 1);
    }

    // Create threads
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, thread_insert_routine, &args[i]);
    }

    // Wait for threads to complete
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    // Verify all inserts were successful
    for (int i = 0; i < NUM_THREADS; i++) {
        for (int j = 0; j < ITERATIONS; j++) {
            TEST_ASSERT_EQUAL_INT(1, args[i].results[j]);
        }
        free(args[i].results);
    }

    // Check final cursor position
    uint64_t expected_cursor = NUM_THREADS * ITERATIONS * DATA_SIZE;
    TEST_ASSERT_EQUAL_UINT64(expected_cursor, atomic_load(&handle->metadata->cursor));

    // Clean up
    free(handle->chunks.buffer);
    free(handle);

    cleanup_test_files();
}

typedef struct {
    log_handle_t* handle;
    int thread_id;
    uint64_t* offsets;
    int num_entries;
    size_t data_size;
    bool success;
} checkout_thread_args_t;

void* checkout_routine(void* arg) {
    checkout_thread_args_t* args = (checkout_thread_args_t*)arg;
    args->success = true;

    for (int i = 0; i < args->num_entries; i++) {
        // Each thread checks out each entry in a different order
        int idx = (i + args->thread_id) % args->num_entries;
        uint64_t offset = args->offsets[idx];

        chunk_info_t* chunk = mmlog_rb_checkout(args->handle, offset);
        if (!chunk) {
            printf("Thread %d: Failed to checkout chunk for offset %lu (%s)(%s)\n", args->thread_id, offset, mmlog_strerror_cur(), strerror(errno));
            args->success = false;
            continue;
        }

        // Verify the data
        uint64_t chunk_offset = offset - chunk->start_offset;
        char* read_data = (char*)chunk->mapping + chunk_offset;

        bool data_valid = true;
        for (size_t j = 0; j < args->data_size; j++) {
            char expected = (char)((idx * 10) + (j % 10));
            if (read_data[j] != expected) {
                data_valid = false;
                break;
            }
        }

        atomic_fetch_sub(&chunk->ref_count, 1);

        if (!data_valid) {
            args->success = false;
        }
    }

    return NULL;
}

void test_mmlog_concurrent_checkout(void) {
    cleanup_test_files();

    log_handle_t* handle = mmlog_open(TEST_LOG_FILENAME, TEST_CHUNK_SIZE, TEST_CHUNK_COUNT);
    TEST_ASSERT_NOT_NULL(handle);

    // Insert test data first
    const size_t DATA_SIZE = 100;
    const int NUM_ENTRIES = 50;

    uint64_t offsets[NUM_ENTRIES];

    // Create and insert distinct data chunks
    for (int i = 0; i < NUM_ENTRIES; i++) {
        char data[DATA_SIZE];
        for (size_t j = 0; j < DATA_SIZE; j++) {
            data[j] = (char)((i * 10) + (j % 10));
        }

        offsets[i] = atomic_load(&handle->metadata->cursor);
        TEST_ASSERT_TRUE(mmlog_insert(handle, data, DATA_SIZE));
    }

    // Now test concurrent checkout with multiple threads
    const int NUM_THREADS = 8;

    pthread_t threads[NUM_THREADS];
    checkout_thread_args_t thread_args[NUM_THREADS];

    // Initialize thread arguments
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_args[i].handle = handle;
        thread_args[i].thread_id = i;
        thread_args[i].offsets = offsets;
        thread_args[i].num_entries = NUM_ENTRIES;
        thread_args[i].data_size = DATA_SIZE;
        thread_args[i].success = false;
    }

    // Create threads
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, checkout_routine, &thread_args[i]);
    }

    // Wait for threads to complete
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
        TEST_ASSERT_TRUE(thread_args[i].success);
    }

    // Clean up
    free(handle->chunks.buffer);
    free(handle);

    cleanup_test_files();
}

void test_mmlog_fork_basic(void) {
    cleanup_test_files();

    log_handle_t* handle = mmlog_open(TEST_LOG_FILENAME, TEST_CHUNK_SIZE, TEST_CHUNK_COUNT);
    TEST_ASSERT_NOT_NULL(handle);

    // Insert initial data from parent
    const char parent_data[] = "Parent process data";
    TEST_ASSERT_TRUE(mmlog_insert(handle, parent_data, strlen(parent_data) + 1));

    pid_t pid = fork();
    TEST_ASSERT_NOT_EQUAL(-1, pid);

    if (pid == 0) {
        // Child process
        const char child_data[] = "Child process data";
        mmlog_insert(handle, child_data, strlen(child_data) + 1);

        // Cleanup in child
        free(handle->chunks.buffer);
        free(handle);
        exit(0);
    } else {
        // Parent process
        int status;
        waitpid(pid, &status, 0);
        TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(status));

        // Verify both parent and child data
        uint64_t parent_offset = 0;
        uint64_t child_offset = strlen(parent_data) + 1;

        // Check parent data
        chunk_info_t* chunk = mmlog_rb_checkout(handle, parent_offset);
        TEST_ASSERT_NOT_NULL(chunk);
        uint64_t offset = parent_offset - chunk->start_offset;
        char* read_data = (char*)chunk->mapping + offset;
        TEST_ASSERT_EQUAL_STRING("Parent process data", read_data);
        atomic_fetch_sub(&chunk->ref_count, 1);

        // Check child data
        chunk = mmlog_rb_checkout(handle, child_offset);
        TEST_ASSERT_NOT_NULL(chunk);
        offset = child_offset - chunk->start_offset;
        read_data = (char*)chunk->mapping + offset;
        TEST_ASSERT_EQUAL_STRING("Child process data", read_data);
        atomic_fetch_sub(&chunk->ref_count, 1);

        // Clean up in parent
        free(handle->chunks.buffer);
        free(handle);
    }

    cleanup_test_files();
}

void test_mmlog_multiple_forks(void) {
    cleanup_test_files();

    log_handle_t* handle = mmlog_open(TEST_LOG_FILENAME, TEST_CHUNK_SIZE, TEST_CHUNK_COUNT);
    TEST_ASSERT_NOT_NULL(handle);

    const int NUM_CHILDREN = 5;
    pid_t children[NUM_CHILDREN];

    // Insert initial parent marker
    const char parent_marker[] = "PARENT";
    TEST_ASSERT_TRUE(mmlog_insert(handle, parent_marker, strlen(parent_marker) + 1));

    // Fork multiple children
    for (int i = 0; i < NUM_CHILDREN; i++) {
        children[i] = fork();
        TEST_ASSERT_NOT_EQUAL(-1, children[i]);

        if (children[i] == 0) {
            // Child process
            char child_data[20];
            snprintf(child_data, sizeof(child_data), "CHILD-%d", i);

            // Insert child-specific data
            for (int j = 0; j < 3; j++) {  // Each child inserts 3 entries
                mmlog_insert(handle, child_data, strlen(child_data) + 1);
            }

            // Cleanup in child
            free(handle->chunks.buffer);
            free(handle);
            exit(0);
        }
    }

    // Parent waits for all children
    for (int i = 0; i < NUM_CHILDREN; i++) {
        int status;
        waitpid(children[i], &status, 0);
        TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(status));
    }

    // Verify final cursor position
    // Parent marker + (NUM_CHILDREN * 3 child entries)
    uint64_t parent_marker_size = strlen(parent_marker) + 1;

    // Allow some flexibility in the exact cursor position due to variable data sizes
    uint64_t actual_cursor = atomic_load(&handle->metadata->cursor);
    TEST_ASSERT_TRUE(actual_cursor >= parent_marker_size + (NUM_CHILDREN * 3 * 8));  // At least this many bytes

    // Clean up in parent
    free(handle->chunks.buffer);
    free(handle);

    cleanup_test_files();
}

void test_mmlog_fork_large_data(void) {
    cleanup_test_files();

    log_handle_t* handle = mmlog_open(TEST_LOG_FILENAME, TEST_CHUNK_SIZE, TEST_CHUNK_COUNT);
    TEST_ASSERT_NOT_NULL(handle);

    // Create a data buffer larger than a chunk
    size_t large_size = TEST_CHUNK_SIZE * 1.5;
    char* large_buffer = (char*)malloc(large_size);
    TEST_ASSERT_NOT_NULL(large_buffer);

    // Fill buffer with a pattern
    for (size_t i = 0; i < large_size; i++) {
        large_buffer[i] = (char)(i % 256);
    }

    pid_t pid = fork();
    TEST_ASSERT_NOT_EQUAL(-1, pid);

    if (pid == 0) {
        // Child process
        mmlog_insert(handle, large_buffer, large_size);

        // Cleanup in child
        free(large_buffer);
        free(handle->chunks.buffer);
        free(handle);
        exit(0);
    } else {
        // Parent process
        int status;
        waitpid(pid, &status, 0);
        TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(status));

        // Verify data was written
        uint64_t cursor = atomic_load(&handle->metadata->cursor);
        TEST_ASSERT_EQUAL_UINT64(large_size, cursor);

        // Verify the data by reading it back
        uint64_t offset = 0;
        size_t bytes_verified = 0;

        while (bytes_verified < large_size) {
            chunk_info_t* chunk = mmlog_rb_checkout(handle, offset);
            TEST_ASSERT_NOT_NULL(chunk);

            uint64_t chunk_offset = offset - chunk->start_offset;
            size_t bytes_to_verify = large_size - bytes_verified;

            // Limit to current chunk
            if (chunk_offset + bytes_to_verify > chunk->size) {
                bytes_to_verify = chunk->size - chunk_offset;
            }

            // Compare data
            char* read_data = (char*)chunk->mapping + chunk_offset;
            for (size_t i = 0; i < bytes_to_verify; i++) {
                TEST_ASSERT_EQUAL_UINT8(large_buffer[bytes_verified + i], read_data[i]);
            }

            atomic_fetch_sub(&chunk->ref_count, 1);

            // Move to next chunk
            offset += bytes_to_verify;
            bytes_verified += bytes_to_verify;
        }

        // Clean up in parent
        free(large_buffer);
        free(handle->chunks.buffer);
        free(handle);
    }

    cleanup_test_files();
}

void test_mmlog_fork_then_thread(void) {
    cleanup_test_files();

    log_handle_t* handle = mmlog_open(TEST_LOG_FILENAME, TEST_CHUNK_SIZE, TEST_CHUNK_COUNT);
    TEST_ASSERT_NOT_NULL(handle);

    // Add a marker to identify parent
    const char parent_marker[] = "PARENT";
    TEST_ASSERT_TRUE(mmlog_insert(handle, parent_marker, strlen(parent_marker) + 1));

    pid_t pid = fork();
    TEST_ASSERT_NOT_EQUAL(-1, pid);

    if (pid == 0) {
        // Child process

        // Add a marker to identify child
        const char child_marker[] = "CHILD";
        mmlog_insert(handle, child_marker, strlen(child_marker) + 1);

        // Now create threads in child
        const int NUM_THREADS = 3;
        pthread_t threads[NUM_THREADS];
        thread_test_args_t thread_args[NUM_THREADS];

        // Initialize thread arguments
        for (int i = 0; i < NUM_THREADS; i++) {
            thread_args[i].handle = handle;
            thread_args[i].thread_id = i;
            thread_args[i].iterations = 5;
            thread_args[i].data_size = 50;
            thread_args[i].results = (char*)calloc(thread_args[i].iterations, 1);
        }

        // Create threads
        for (int i = 0; i < NUM_THREADS; i++) {
            pthread_create(&threads[i], NULL, thread_insert_routine, &thread_args[i]);
        }

        // Wait for threads to complete
        for (int i = 0; i < NUM_THREADS; i++) {
            pthread_join(threads[i], NULL);

            // Check that all inserts succeeded
            for (int j = 0; j < thread_args[i].iterations; j++) {
                if (thread_args[i].results[j] != 1) {
                    exit(1);  // Signal failure
                }
            }
            free(thread_args[i].results);
        }

        // Add another marker after threads complete
        const char child_end_marker[] = "CHILD-END";
        mmlog_insert(handle, child_end_marker, strlen(child_end_marker) + 1);

        // Cleanup in child
        free(handle->chunks.buffer);
        free(handle);
        exit(0);
    } else {
        // Parent process
        int status;
        waitpid(pid, &status, 0);
        TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(status));

        // Check final log state
        uint64_t cursor = atomic_load(&handle->metadata->cursor);
        TEST_ASSERT_TRUE(cursor > 0);

        // Verify parent marker is at the beginning
        chunk_info_t* chunk = mmlog_rb_checkout(handle, 0);
        TEST_ASSERT_NOT_NULL(chunk);
        TEST_ASSERT_EQUAL_STRING("PARENT", (char*)chunk->mapping);
        atomic_fetch_sub(&chunk->ref_count, 1);

        // Clean up in parent
        free(handle->chunks.buffer);
        free(handle);
    }

    cleanup_test_files();
}

void test_mmlog_thread_then_fork(void) {
    cleanup_test_files();

    log_handle_t* handle = mmlog_open(TEST_LOG_FILENAME, TEST_CHUNK_SIZE, TEST_CHUNK_COUNT);
    TEST_ASSERT_NOT_NULL(handle);

    // First create threads in the parent
    const int NUM_PARENT_THREADS = 3;
    pthread_t parent_threads[NUM_PARENT_THREADS];
    thread_test_args_t parent_thread_args[NUM_PARENT_THREADS];

    // Initialize thread arguments
    for (int i = 0; i < NUM_PARENT_THREADS; i++) {
        parent_thread_args[i].handle = handle;
        parent_thread_args[i].thread_id = i;
        parent_thread_args[i].iterations = 5;
        parent_thread_args[i].data_size = 50;
        parent_thread_args[i].results = (char*)calloc(parent_thread_args[i].iterations, 1);
    }

    // Create threads
    for (int i = 0; i < NUM_PARENT_THREADS; i++) {
        pthread_create(&parent_threads[i], NULL, thread_insert_routine, &parent_thread_args[i]);
    }

    // Wait for threads to complete
    for (int i = 0; i < NUM_PARENT_THREADS; i++) {
        pthread_join(parent_threads[i], NULL);

        // Check that all inserts succeeded
        for (int j = 0; j < parent_thread_args[i].iterations; j++) {
            TEST_ASSERT_EQUAL_INT(1, parent_thread_args[i].results[j]);
        }
        free(parent_thread_args[i].results);
    }

    // Add a marker after parent threads complete
    const char parent_marker[] = "PARENT-THREADS-DONE";
    TEST_ASSERT_TRUE(mmlog_insert(handle, parent_marker, strlen(parent_marker) + 1));

    // Record cursor position for later verification
    uint64_t parent_cursor = atomic_load(&handle->metadata->cursor);

    // Now fork
    pid_t pid = fork();
    TEST_ASSERT_NOT_EQUAL(-1, pid);

    if (pid == 0) {
        // Child process

        // Add a marker to identify child
        const char child_marker[] = "CHILD";
        mmlog_insert(handle, child_marker, strlen(child_marker) + 1);

        // Do some more inserts in the child
        for (int i = 0; i < 10; i++) {
            char child_data[20];
            snprintf(child_data, sizeof(child_data), "CHILD-DATA-%d", i);
            mmlog_insert(handle, child_data, strlen(child_data) + 1);
        }

        // Cleanup in child
        free(handle->chunks.buffer);
        free(handle);
        exit(0);
    } else {
        // Parent process
        int status;
        waitpid(pid, &status, 0);
        TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(status));

        // Check final log state
        uint64_t cursor = atomic_load(&handle->metadata->cursor);
        TEST_ASSERT_TRUE(cursor > parent_cursor);

        // Verify parent marker is at the expected position
        chunk_info_t* chunk = mmlog_rb_checkout(handle, parent_cursor - strlen(parent_marker) - 1);
        TEST_ASSERT_NOT_NULL(chunk);
        uint64_t offset = (parent_cursor - strlen(parent_marker) - 1) - chunk->start_offset;
        TEST_ASSERT_EQUAL_STRING("PARENT-THREADS-DONE", (char*)chunk->mapping + offset);
        atomic_fetch_sub(&chunk->ref_count, 1);

        // Clean up in parent
        free(handle->chunks.buffer);
        free(handle);
    }

    cleanup_test_files();
}

void test_mmlog_stress_test(void) {
    cleanup_test_files();

    log_handle_t* handle = mmlog_open(TEST_LOG_FILENAME, TEST_CHUNK_SIZE, TEST_CHUNK_COUNT);
    TEST_ASSERT_NOT_NULL(handle);

    // First, create multiple processes
    const int NUM_PROCESSES = 3;
    pid_t children[NUM_PROCESSES];

    for (int i = 0; i < NUM_PROCESSES; i++) {
        children[i] = fork();
        TEST_ASSERT_NOT_EQUAL(-1, children[i]);

        if (children[i] == 0) {
            // Child process

            // Now create multiple threads in each child
            const int NUM_THREADS = 3;
            pthread_t threads[NUM_THREADS];
            thread_test_args_t thread_args[NUM_THREADS];

            // Initialize thread arguments
            for (int j = 0; j < NUM_THREADS; j++) {
                thread_args[j].handle = handle;
                thread_args[j].thread_id = i * 100 + j;  // Unique ID across all processes/threads
                thread_args[j].iterations = 10;
                thread_args[j].data_size = 50;
                thread_args[j].results = (char*)calloc(thread_args[j].iterations, 1);
            }

            // Create threads
            for (int j = 0; j < NUM_THREADS; j++) {
                pthread_create(&threads[j], NULL, thread_insert_routine, &thread_args[j]);
            }

            // Wait for threads to complete
            for (int j = 0; j < NUM_THREADS; j++) {
                pthread_join(threads[j], NULL);

                // Check that all inserts succeeded
                for (int k = 0; k < thread_args[j].iterations; k++) {
                    if (thread_args[j].results[k] != 1) {
                        exit(1);  // Signal failure
                    }
                }
                free(thread_args[j].results);
            }

            // Add a process-specific marker
            char process_marker[30];
            snprintf(process_marker, sizeof(process_marker), "PROCESS-%d-DONE", i);
            mmlog_insert(handle, process_marker, strlen(process_marker) + 1);

            // Cleanup in child
            free(handle->chunks.buffer);
            free(handle);
            exit(0);
        }
    }

    // Parent waits for all children
    for (int i = 0; i < NUM_PROCESSES; i++) {
        int status;
        waitpid(children[i], &status, 0);
        TEST_ASSERT_EQUAL_INT(0, WEXITSTATUS(status));
    }

    // Verify final state
    uint64_t cursor = atomic_load(&handle->metadata->cursor);
    // We expect approximately: NUM_PROCESSES * NUM_THREADS * ITERATIONS * DATA_SIZE + some markers
    TEST_ASSERT_TRUE(cursor > 3 * 3 * 10 * 50);

    // Clean up
    free(handle->chunks.buffer);
    free(handle);

    cleanup_test_files();
}

void test_mmlog_data_integrity(void) {
    cleanup_test_files();

    log_handle_t* handle = mmlog_open(TEST_LOG_FILENAME, TEST_CHUNK_SIZE, TEST_CHUNK_COUNT);
    TEST_ASSERT_NOT_NULL(handle);

    // Test with various data types and patterns

    // 1. Test with a string
    const char test_string[] = "This is a test string with NULL bytes \0 embedded in it.";
    size_t string_size = sizeof(test_string);  // Includes all bytes, even after NULL
    uint64_t string_offset = atomic_load(&handle->metadata->cursor);
    TEST_ASSERT_TRUE(mmlog_insert(handle, test_string, string_size));

    // 2. Test with binary data
    uint8_t binary_data[256];
    for (int i = 0; i < 256; i++) {
        binary_data[i] = (uint8_t)i;
    }
    uint64_t binary_offset = atomic_load(&handle->metadata->cursor);
    TEST_ASSERT_TRUE(mmlog_insert(handle, binary_data, sizeof(binary_data)));

    // 3. Test with a structured data type
    typedef struct {
        int32_t a;
        double b;
        uint64_t c;
        char d[32];
    } test_struct_t;

    test_struct_t test_struct = {
        .a = 12345,
        .b = 3.14159265358979323846,
        .c = 0xDEADBEEFCAFEBABE,
        .d = "Hello, structured data!"
    };
    uint64_t struct_offset = atomic_load(&handle->metadata->cursor);
    TEST_ASSERT_TRUE(mmlog_insert(handle, &test_struct, sizeof(test_struct)));

    // Now verify all the data

    // 1. Verify string
    chunk_info_t* chunk = mmlog_rb_checkout(handle, string_offset);
    TEST_ASSERT_NOT_NULL(chunk);
    uint64_t offset = string_offset - chunk->start_offset;
    char* read_string = (char*)chunk->mapping + offset;

    // Verify entire binary content, not just as a C string
    TEST_ASSERT_EQUAL_MEMORY(test_string, read_string, string_size);
    atomic_fetch_sub(&chunk->ref_count, 1);

    // 2. Verify binary data
    chunk = mmlog_rb_checkout(handle, binary_offset);
    TEST_ASSERT_NOT_NULL(chunk);
    offset = binary_offset - chunk->start_offset;
    uint8_t* read_binary = (uint8_t*)chunk->mapping + offset;

    TEST_ASSERT_EQUAL_MEMORY(binary_data, read_binary, sizeof(binary_data));
    atomic_fetch_sub(&chunk->ref_count, 1);

    // 3. Verify structured data
    chunk = mmlog_rb_checkout(handle, struct_offset);
    TEST_ASSERT_NOT_NULL(chunk);
    offset = struct_offset - chunk->start_offset;
    test_struct_t* read_struct = (test_struct_t*)((char*)chunk->mapping + offset);

    TEST_ASSERT_EQUAL_INT32(test_struct.a, read_struct->a);
    TEST_ASSERT_EQUAL_DOUBLE(test_struct.b, read_struct->b);
    TEST_ASSERT_EQUAL_UINT64(test_struct.c, read_struct->c);
    TEST_ASSERT_EQUAL_STRING(test_struct.d, read_struct->d);
    atomic_fetch_sub(&chunk->ref_count, 1);

    // Clean up
    free(handle->chunks.buffer);
    free(handle);

    cleanup_test_files();
}

void test_mmlog_random_data(void) {
    cleanup_test_files();

    log_handle_t* handle = mmlog_open(TEST_LOG_FILENAME, TEST_CHUNK_SIZE, TEST_CHUNK_COUNT);
    TEST_ASSERT_NOT_NULL(handle);

    // Generate and insert random data
    const int NUM_ENTRIES = 100;
    const size_t ENTRY_SIZE = 1024;

    uint64_t offsets[NUM_ENTRIES];
    uint8_t* data_copies[NUM_ENTRIES];

    // Seed random number generator
    srand((unsigned int)time(NULL));

    for (int i = 0; i < NUM_ENTRIES; i++) {
        // Create random data
        uint8_t* random_data = (uint8_t*)malloc(ENTRY_SIZE);
        TEST_ASSERT_NOT_NULL(random_data);

        for (size_t j = 0; j < ENTRY_SIZE; j++) {
            random_data[j] = (uint8_t)(rand() % 256);
        }

        // Insert and store offset
        offsets[i] = atomic_load(&handle->metadata->cursor);
        TEST_ASSERT_TRUE(mmlog_insert(handle, random_data, ENTRY_SIZE));

        // Keep a copy for verification
        data_copies[i] = random_data;
    }

    // Verify all entries
    for (int i = 0; i < NUM_ENTRIES; i++) {
        chunk_info_t* chunk = mmlog_rb_checkout(handle, offsets[i]);
        TEST_ASSERT_NOT_NULL(chunk);

        uint64_t offset = offsets[i] - chunk->start_offset;
        uint8_t* read_data = (uint8_t*)chunk->mapping + offset;

        TEST_ASSERT_EQUAL_MEMORY(data_copies[i], read_data, ENTRY_SIZE);

        atomic_fetch_sub(&chunk->ref_count, 1);
        free(data_copies[i]);
    }

    // Clean up
    free(handle->chunks.buffer);
    free(handle);

    cleanup_test_files();
}

void test_mmlog_trim_validation(void) {
    cleanup_test_files();

    log_handle_t* handle = mmlog_open(TEST_LOG_FILENAME, TEST_CHUNK_SIZE, TEST_CHUNK_COUNT);
    TEST_ASSERT_NOT_NULL(handle);

    // Insert some data
    const char* test_strings[] = {
        "First entry",
        "Second entry",
        "Third entry",
        "Fourth entry"
    };

    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(mmlog_insert(handle, test_strings[i], strlen(test_strings[i]) + 1));
    }

    // Get current cursor
    uint64_t cursor_before_trim = atomic_load(&handle->metadata->cursor);

    // Trim the log
    mmlog_trim(handle);

    // Make sure cursor didn't change
    uint64_t cursor_after_trim = atomic_load(&handle->metadata->cursor);
    TEST_ASSERT_EQUAL_UINT64(cursor_before_trim, cursor_after_trim);

    // Insert more data
    const char* post_trim_string = "After trim entry";
    TEST_ASSERT_TRUE(mmlog_insert(handle, post_trim_string, strlen(post_trim_string) + 1));

    // Read back the last entry to verify it was properly written
    uint64_t last_offset = cursor_after_trim;
    chunk_info_t* chunk = mmlog_rb_checkout(handle, last_offset);
    TEST_ASSERT_NOT_NULL(chunk);

    uint64_t offset = last_offset - chunk->start_offset;
    char* read_string = (char*)chunk->mapping + offset;

    TEST_ASSERT_EQUAL_STRING(post_trim_string, read_string);
    atomic_fetch_sub(&chunk->ref_count, 1);

    // Clean up
    free(handle->chunks.buffer);
    free(handle);

    cleanup_test_files();
}

int main(void) {
    UNITY_BEGIN();

    // Basic functionality tests
    RUN_TEST(test_mmlog_open_and_close);
    RUN_TEST(test_mmlog_insert_and_checkout);
    RUN_TEST(test_mmlog_multiple_inserts);
    RUN_TEST(test_mmlog_large_insert);

    // Threading tests
    RUN_TEST(test_mmlog_concurrent_inserts);
    RUN_TEST(test_mmlog_concurrent_checkout);

    // Forking tests
    RUN_TEST(test_mmlog_fork_basic);
    RUN_TEST(test_mmlog_multiple_forks);
    RUN_TEST(test_mmlog_fork_large_data);

    // Combined threading and forking tests
    RUN_TEST(test_mmlog_fork_then_thread);
    RUN_TEST(test_mmlog_thread_then_fork);
    RUN_TEST(test_mmlog_stress_test);

    // Data validation tests
    RUN_TEST(test_mmlog_data_integrity);
    RUN_TEST(test_mmlog_random_data);
    RUN_TEST(test_mmlog_trim_validation);

    return UNITY_END();
}
