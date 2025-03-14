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
    bool is_untracked;                  // A special chunk that is used to back "temporary" ranges
    uint64_t start_offset;              // Start offset in file
    uint64_t size;                      // Size of this chunk
    void* mapping;                      // Pointer to mapped memory
    _Atomic uint32_t ref_count;         // Number of active users (atomic)
} chunk_info_t;

typedef struct {
    chunk_info_t** buffer;  // Buffer for chunk_info_t (maybe make this configurable?)
    size_t capacity;        // Capacity of the buffer
    _Atomic size_t head;    // Where to put the next chunk (head is unavailable)
    _Atomic size_t tail;    // Oldest chunk (tail is available)
} chunk_buffer_t;

// Process-local state
typedef struct {
    int metadata_fd;               // File descriptor for metadata
    int data_fd;                   // File descriptor for data
    log_metadata_t* metadata;      // Pointer to mapped metadata
    chunk_buffer_t chunks;         // Ringbuffer for current chunks
} log_handle_t;

// Range returned from checkout operation
typedef struct {
    bool is_valid;          // True if this range is valid
    uint64_t start;         // Start position in log
    uint64_t end;           // End position in log
    chunk_info_t* chunk;    // Pointer to the chunk containing this range
    uint64_t chunk_offset;  // Offset into the chunk where this range begins
} log_range_t;

// Sentinel value for invalid cursor
#define LOG_CURSOR_INVALID ((uint64_t)-1)

// Sentinel value for chunk in intermediate state
#define CHUNK_PENDING (chunk_info_t*)1


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
    int fd_data = -1;
    int64_t start_time = 0;
    int64_t cur_time = 0;
    log_metadata_t* metadata = NULL;
    if (-1 == fd_meta) {
        return false;
    }

    // Before we mmap the metadata, check the file size.  If it's nonzero, but less than the size of the metadata, it's
    // probable that we have an incompatible metadata version--so bail out
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
    start_time = ms_since_epoch_monotonic();
    cur_time = start_time;
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
    fd_data = open(filename, O_RDWR);
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

log_handle_t* mmlog_open(const char* filename, size_t chunk_size, uint32_t chunk_count)
{
    // Validate chunk size (must be multiple of page size)
    if (chunk_size % LOG_PAGE_SIZE != 0 || chunk_size == 0 || chunk_count == 0) {
        errno = EINVAL;
        return NULL;
    }

    // Allocate handle
    log_handle_t* handle = (log_handle_t*)calloc(1, sizeof(log_handle_t));
    if (!handle) {
        return NULL;
    }

    // Allocate chunk buffer
    handle->chunks.capacity = chunk_count;
    handle->chunks.buffer = (chunk_info_t**)calloc(chunk_count, sizeof(chunk_info_t*));
    if (!handle->chunks.buffer) {
        free(handle);
        return NULL;
    }

    for (uint32_t i = 0; i < chunk_count; i++) {
        handle->chunks.buffer[i] = NULL;
    }

    if (!files_open_or_create(filename, chunk_size, handle)) {
        free(handle->chunks.buffer);
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

uint64_t mmlog_checkout(log_handle_t* handle, size_t size)
{
    // Gets the start position for the requested write operation
    // Will increase the capacity of the file if needed, but does not map the new pages
    if (!handle || !handle->metadata) {
        errno = EINVAL;
        return LOG_CURSOR_INVALID;
    }

    log_metadata_t* metadata = handle->metadata;
    uint64_t start = atomic_fetch_add(&metadata->cursor, size);
    uint64_t end = start + size;
    uint64_t file_size = atomic_load(&metadata->file_size);

    if (end > file_size) {
        if (!data_file_expand(handle, end)) {
            return LOG_CURSOR_INVALID;
        }
    }
    return start;
}


bool mmlog_clean_chunks(log_handle_t *handle)
{
    // Cleans chunks from the ringbuffer if they are no longer in use
    if (!handle || !handle->metadata) {
        errno = EINVAL;
        return false;
    }
    chunk_buffer_t *chunks = &handle->chunks;

    while (true) {
        size_t tail_index = atomic_load(&chunks->tail);
        if (tail_index == atomic_load(&chunks->head)) {
            // No chunks to clean
            return true;
        }

        chunk_info_t *tail = chunks->buffer[tail_index];
        if (!tail || tail == CHUNK_PENDING || atomic_load(&tail->ref_count) != 0) {
            break;
        }

        // Calculate the new tail position with wraparound
        size_t new_tail_index = (tail_index + 1) % chunks->capacity;

        // Attempt to update the tail index atomically
        if (atomic_compare_exchange_strong(&chunks->tail, &tail_index, new_tail_index)) {
            chunks->buffer[tail_index] = NULL;
            munmap(tail->mapping, tail->size);
            free(tail);
            return true;
        }
    }

    return true;
}


bool mmlog_checkin(log_handle_t* handle, log_range_t* range)
{
}

 uint32_t mmlog_cross_count(uint64_t start, size_t size, uint32_t chunk_size)
{
    uint64_t end = start + size;

    // Calculate the chunk indices for the start and end positions
    uint64_t start_chunk_index = start / chunk_size;
    uint64_t end_chunk_index = end / chunk_size;

    // Check if the request crosses two chunk boundaries
    return end_chunk_index - start_chunk_index;
}

bool mmlog_insert(log_handle_t* handle, const void* data, size_t size)
{
    if (!handle || !data || size == 0) {
        errno = EINVAL;
        return false;
    }

    log_metadata_t *metadata = handle->metadata;

    uint64_t cursor = mmlog_checkout(handle, size);
    if (cursor == LOG_CURSOR_INVALID) {
        return false;
    }

    // With this, we have a contiguous range to write to, but it needs to be in-memory for us to map it.  Consider a few scenarios
    // 1. The data fits in a chunk (although it may cross up to one chunk boundary)--check/add to ringbuffer
    // 2. The data crosses multiple chunks--just do a one-off mmap for this operation
    if (mmlog_cross_count(cursor, size, metadata->chunk_size) > 1) {
        // If we have more than one crossing, it means that the intermediate chunk is going to be used solely for this operation;
        // so there is no point in using the ringbuffer--just map everything specifically for this operation
    } else if () {
        // If the span straddles a chunk in its entirety, then we also avoid using the ringbuffer

    } else {
        // If we have one or fewer crossings, then it's conceivable that multiple processes will be writing to the same chunk
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
