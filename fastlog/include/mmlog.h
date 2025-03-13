#pragma once

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

// Constants
#define LOG_PAGE_SIZE 4096    // Fixed page size
#define SLEEP_TIME_MAX_MS 10  // Maximum sleep time in milliseconds (4 + 4 + a bit)
#define MMLOG_VERSION 1       // Current version of the log format

// Alignment macro
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))

typedef struct {
    uint32_t version;            // Format version number
    _Atomic bool is_ready;       // Initialization flag (true when fully initialized)
    _Atomic bool is_locked;      // Lock flag for file extension
    _Atomic bool is_panicked;    // Panic flag (true when log is in an inconsistent state)
    _Atomic uint64_t file_size;  // Current physical size of the data file
    _Atomic uint64_t cursor;     // Current append position
    uint32_t page_size;          // Fixed page size value
    uint32_t chunk_size;         // Size of chunks (multiple of page_size)
} log_metadata_t;

// Chunk tracking (returned at checkout time)
typedef struct chunk_info_t {
    void* mapping;                      // Pointer to mapped memory
    uint64_t start_offset;              // Start offset in file
    uint64_t size;                      // Size of this chunk
    _Atomic uint32_t ref_count;         // Number of active users (atomic)
    struct chunk_info_t* _Atomic next;  // Next chunk in list (atomic pointer)
    _Atomic bool is_unmappable;         // True when this chunk can be unmapped (not the tail)
} chunk_info_t;

// Process-local state
typedef struct {
    int metadata_fd;               // File descriptor for metadata
    int data_fd;                   // File descriptor for data
    log_metadata_t* metadata;      // Pointer to mapped metadata
    chunk_info_t* _Atomic head;    // Head of chunk list (oldest chunk)
    chunk_info_t* _Atomic tail;    // Tail of chunk list (newest chunk, where new writes go)
    _Atomic uint32_t chunk_count;  // Number of chunks in the list
} log_handle_t;

// Range returned from checkout operation
typedef struct {
    bool is_valid;          // True if this range is valid
    uint64_t start;         // Start position in log
    uint64_t end;           // End position in log
    chunk_info_t* chunk;    // Pointer to the chunk containing this range
    uint64_t chunk_offset;  // Offset into the chunk where this range begins
} log_range_t;

int64_t ms_since_epoch_monotonic(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return -1;  // I mean, sure, but will anybody realistically ever check?
    }
    return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
}

bool files_create(int fd_meta, const char* filename, uint32_t chunk_size, log_handle_t* handle)
{
    ftruncate(fd_meta, sizeof(log_metadata_t));
    log_metadata_t* metadata =
        (log_metadata_t*)mmap(NULL, sizeof(log_metadata_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd_meta, 0);
    int fd_data = -1;
    if (MAP_FAILED == metadata) {
        goto files_create_cleanup;
    }

    metadata->version = MMLOG_VERSION;
    metadata->page_size = LOG_PAGE_SIZE;
    metadata->chunk_size = chunk_size;

    // We're going to try and open the data file, but it's a bit tricky.
    // 1. if it doesn't exist--great!
    // 2. if it does exist, it's hard to figure out where to put the cursor, and we don't want to corrupt incoming
    // binary data. For this implementation, just clobber the file!
    fd_data = open(filename, O_RDWR | O_CREAT, 0644);
    if (-1 == fd_data) {
        goto files_create_cleanup;
    }

    ftruncate(fd_data, 0);  // zero out the file
    if (-1 == ftruncate(fd_data, metadata->chunk_size)) {
        goto files_create_cleanup;
    }

    atomic_store(&metadata->file_size, chunk_size);
    atomic_store(&metadata->is_ready, true);
    handle->metadata = metadata;
    handle->data_fd = fd_data;
    handle->metadata_fd = fd_meta;
    return true;

files_create_cleanup:
    munmap(metadata, sizeof(log_metadata_t));
    close(fd_data);
    return false;
}

bool files_open(const char* filename, const char* meta_filename, log_handle_t* handle)
{
    int fd_meta = open(meta_filename, O_RDWR);
    log_metadata_t* metadata = NULL;
    if (-1 == fd_meta) {
        return false;
    }

    // Before we mmap the metadata, check the file size.  If it's nonzero, but less than the size of the metadata, it's
    // probable that we have an incompatable metadata version--so bail out
    int retries = 3;
    bool is_ok = false;
    while (--retries) {
        struct stat st;
        if (fstat(fd_meta, &st) != 0 || (size_t)st.st_size < sizeof(log_metadata_t)) {
            continue;
        }
        is_ok = true;
        break;
    }

    if (!is_ok) {
        goto files_open_cleanup;
    }

    // Nothing is valid until we've mapped the metadata
    metadata = (log_metadata_t*)mmap(NULL, sizeof(log_metadata_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd_meta, 0);
    if (MAP_FAILED == metadata || metadata->version != MMLOG_VERSION) {
        goto files_open_cleanup;
    }

    // Sit in a try-sleep loop for checking readiness
    int64_t start_time = ms_since_epoch_monotonic();
    int64_t cur_time = start_time;
    is_ok = false;
    while (cur_time - start_time < SLEEP_TIME_MAX_MS) {
        if (atomic_load(&metadata->is_ready)) {
            is_ok = true;
            break;
        }
        sched_yield();
        cur_time = ms_since_epoch_monotonic();
    }

    if (!is_ok) {
        goto files_open_cleanup;
    }

    // Finally, we have to open the data file
    int fd_data = open(filename, O_RDWR);
    if (-1 == fd_data) {
        goto files_open_cleanup;
    }

    handle->metadata_fd = fd_meta;
    handle->data_fd = fd_data;
    handle->metadata = metadata;

    return true;

files_open_cleanup:
    munmap(metadata, sizeof(log_metadata_t));
    close(fd_meta);
    return false;
}

bool files_open_or_create(const char* filename, uint32_t chunk_size, log_handle_t* handle)
{
    static const char* suffix = ".mmlog";
    size_t meta_filename_len = strlen(filename) + strlen(suffix) + 1;
    char* meta_filename = (char*)malloc(meta_filename_len);
    int fd_meta = -1;
    if (!meta_filename) {
        goto files_open_or_create_cleanup;
    }

    if (snprintf(meta_filename, meta_filename_len, "%s.mmlog", filename) < 0) {
        goto files_open_or_create_cleanup;
    }

    // Try to create metadata file (exclusive create)
    fd_meta = open(meta_filename, O_RDWR | O_CREAT | O_EXCL, 0644);
    if (-1 == fd_meta) {
        if (!files_open(filename, meta_filename, handle)) {
            goto files_open_or_create_cleanup;
        }
    } else {
        if (!files_create(fd_meta, filename, chunk_size, handle)) {
            goto files_open_or_create_cleanup;
        }
    }

    return true;

files_open_or_create_cleanup:
    free(meta_filename);
    close(fd_meta);
    return false;
}

log_handle_t* mmlog_open(const char* filename, size_t chunk_size)
{
    // Validate chunk size (must be multiple of page size)
    if (chunk_size % LOG_PAGE_SIZE != 0 || chunk_size == 0) {
        errno = EINVAL;
        return NULL;
    }

    // Allocate handle
    log_handle_t* handle = (log_handle_t*)calloc(1, sizeof(log_handle_t));
    if (!handle) {
        return NULL;
    }

    if (!files_open_or_create(filename, chunk_size, handle)) {
        free(handle);
        return NULL;
    }

    return handle;
}

bool data_file_expand(log_handle_t* handle, uint64_t end)
{
    log_metadata_t* metadata = handle->metadata;
    bool expected = false;

    int64_t start_time = ms_since_epoch_monotonic();
    int64_t cur_time = start_time;
    while (cur_time - start_time < SLEEP_TIME_MAX_MS) {
        if (atomic_load(&metadata->is_panicked)) {
            return false;
        }

        if (atomic_compare_exchange_strong(&metadata->is_locked, &expected, true)) {
            uint64_t file_size = atomic_load(&metadata->file_size);
            if (file_size < end) {
                uint64_t new_size = ALIGN(end, metadata->chunk_size);
                if (-1 == ftruncate(handle->data_fd, new_size)) {
                    // Whoa, we can't ftruncate!  Everything sucks!
                    atomic_store(&metadata->is_panicked, true);
                    atomic_store(&metadata->is_locked, false);
                    return false;
                }
                atomic_store(&metadata->file_size, new_size);
                atomic_store(&metadata->is_locked, false);
                file_size = new_size;
                return true;
            }
        } else {
            // Can't take the lock, which probably means someone else has it
            // Let's try again later
            sched_yield();
            uint64_t file_size = atomic_load(&metadata->file_size);
            if (file_size >= end) {
                return true;
            }
        }

        cur_time = ms_since_epoch_monotonic();
    }

    // If we're here, then we timed out
    return false;
}

log_range_t mmlog_checkout(log_handle_t* handle, size_t size)
{
    log_range_t result = {0};

    // Validate parameters
    if (!handle || !handle->metadata || size == 0) {
        perror("mmlog_checkout: Invalid arguments");
        errno = EINVAL;
        return result;
    }

    log_metadata_t* metadata = handle->metadata;
    uint32_t chunk_size = metadata->chunk_size;

    // Atomically claim the range
    uint64_t start = atomic_fetch_add(&metadata->cursor, size);
    uint64_t end = start + size;

    // Calculate chunk boundaries for this range
    uint64_t chunk_start = (start / chunk_size) * chunk_size;
    uint64_t chunk_end = chunk_start + chunk_size;

    // Get the current tail chunk - safe to access with atomic_load
    chunk_info_t* tail = atomic_load(&handle->tail);
    chunk_info_t* chunk = NULL;

    // Check if we have a tail and if the requested range is within it
    if (tail && tail->start_offset == chunk_start) {
        // The range falls within the current tail chunk
        chunk = tail;
        atomic_fetch_add(&chunk->ref_count, 1);
    } else {
        // Need to check if we need to expand the file
        uint64_t file_size = atomic_load(&metadata->file_size);
        if (end > file_size) {
            if (!data_file_expand(handle, end)) {
                perror("mmlog_checkout: Failed to expand file");
                return result;
            }
            // Get the updated file size after expansion
            file_size = atomic_load(&metadata->file_size);
        }

        // Need to create a new chunk
        chunk = (chunk_info_t*)calloc(1, sizeof(chunk_info_t));
        if (!chunk) {
            perror("mmlog_checkout: Failed to allocate chunk");
            errno = ENOMEM;
            return result;
        }

        // Map the chunk
        chunk->mapping = mmap(NULL, chunk_size, PROT_READ | PROT_WRITE, MAP_SHARED, handle->data_fd, chunk_start);
        if (chunk->mapping == MAP_FAILED) {
            perror("mmlog_checkout: Failed to map chunk");
            free(chunk);
            errno = ENOMEM;
            return result;
        }

        // Initialize chunk info
        chunk->start_offset = chunk_start;
        chunk->size = chunk_end < file_size ? chunk_size : (file_size - chunk_start);
        atomic_store(&chunk->ref_count, 1);
        atomic_store(&chunk->next, NULL);
        atomic_store(&chunk->is_unmappable, false);  // New chunk is not unmappable yet

        // Update the linked list - handle first-time initialization safely
        if (atomic_load(&handle->head) == NULL) {
            // This is the first chunk
            atomic_store(&handle->head, chunk);
            atomic_store(&handle->tail, chunk);
            atomic_store(&handle->chunk_count, 1);
        } else {
            // We know tail exists since head exists
            tail = atomic_load(&handle->tail);

            // Mark previous tail as unmappable
            atomic_store(&tail->is_unmappable, true);

            // Add the new chunk to the list
            atomic_store(&tail->next, chunk);
            atomic_store(&handle->tail, chunk);
            atomic_fetch_add(&handle->chunk_count, 1);
        }
    }

    // Set up the result
    result.is_valid = true;
    result.start = start;
    result.end = end;
    result.chunk = chunk;
    result.chunk_offset = start - chunk_start;

    return result;
}

bool mmlog_checkin(log_handle_t* handle, log_range_t* range)
{
    if (!handle || !range || !range->is_valid || !range->chunk) {
        errno = EINVAL;
        return false;
    }

    chunk_info_t* chunk = range->chunk;

    // Decrement the reference count
    uint32_t old_count = atomic_fetch_sub(&chunk->ref_count, 1);

    // If this was the last reference and the chunk is unmappable, clean it up
    if (old_count == 1 && atomic_load(&chunk->is_unmappable)) {
        // Unmap the chunk
        if (munmap(chunk->mapping, chunk->size) != 0) {
            // I literally have no idea--this is very wrong
            int* p = NULL;
            *p = 0;  // crash and burn
        }

        // Safety check for handle->head
        if (atomic_load(&handle->head) == NULL) {
            // Something is wrong - inconsistent state
            range->is_valid = false;
            return true;  // Still return true to prevent caller from hanging
        }

        // Remove from the list - this is a complete traversal to ensure we clean up
        // any chunk, not just the head
        chunk_info_t* curr = atomic_load(&handle->head);
        chunk_info_t* prev = NULL;

        while (curr) {
            if (curr == chunk) {
                // Found the chunk to remove
                chunk_info_t* next = atomic_load(&curr->next);

                if (prev) {
                    // Not the head - update previous node's next pointer
                    atomic_store(&prev->next, next);
                } else {
                    // This is the head - update the head pointer
                    atomic_store(&handle->head, next);
                }

                // If this was the tail, update the tail pointer
                if (atomic_load(&handle->tail) == chunk) {
                    atomic_store(&handle->tail, prev);
                }

                atomic_fetch_sub(&handle->chunk_count, 1);
                free(chunk);
                break;
            }

            prev = curr;
            curr = atomic_load(&curr->next);
        }
    }

    // Mark the range as invalid to prevent reuse
    range->is_valid = false;

    return true;
}

bool mmlog_insert(log_handle_t* handle, const void* data, size_t size)
{
    if (!handle || !data || size == 0) {
        perror("Invalid arguments");
        errno = EINVAL;
        return false;
    }

    size_t remaining_size = size;
    const char* data_ptr = (const char*)data;

    while (remaining_size > 0) {
        // Check out a range for writing
        log_range_t range = mmlog_checkout(handle, remaining_size);
        if (!range.is_valid) {
            perror("Failed to checkout range");
            return false;
        }

        // Calculate the size to copy in this iteration
        size_t copy_size = range.end - range.start;
        if (copy_size > remaining_size) {
            copy_size = remaining_size;
        }

        // Copy the data into the range
        void* target = (char*)range.chunk->mapping + range.chunk_offset;
        memcpy(target, data_ptr, copy_size);

        // Update pointers and remaining size
        data_ptr += copy_size;
        remaining_size -= copy_size;

        // Check the range back in
        if (!mmlog_checkin(handle, &range)) {
            perror("Failed to checkin range");
            return false;
        }
    }

    return true;
}

void mmlog_trim(log_handle_t* handle)
{
    // Trims the log file to the current cursor position
    if (!handle || !handle->metadata) {
        return;
    }

    log_metadata_t* metadata = handle->metadata;
    uint64_t cursor = atomic_load(&metadata->cursor);
    ftruncate(handle->data_fd, cursor);
}
