#pragma once

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
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
    bool is_untracked;           // A special chunk that is used to back "temporary" ranges
    uint64_t start_offset;       // Start offset in file
    uint64_t size;               // Size of this chunk
    void* mapping;               // Pointer to mapped memory
    _Atomic uint32_t ref_count;  // Number of active users (atomic)
} chunk_info_t;

typedef struct {
    _Atomic(chunk_info_t**) buffer;  // Buffer for chunk_info_t (maybe make this configurable?)
    size_t capacity;                 // Capacity of the buffer
    _Atomic(chunk_info_t**) head;    // Pointer to the current chunk (head is _not_ available)
    _Atomic(chunk_info_t**) tail;    // Pointer to the next chunk (tail is available)
    _Atomic bool head_locked;        // Lock for head advancement operations
} chunk_buffer_t;

// Process-local state
typedef struct {
    int metadata_fd;           // File descriptor for metadata
    int data_fd;               // File descriptor for data
    log_metadata_t* metadata;  // Pointer to mapped metadata
    chunk_buffer_t chunks;     // Ringbuffer for current chunks
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
    log_metadata_t* metadata;
    int fd_data = -1;
    if (-1 == ftruncate(fd_meta, sizeof(log_metadata_t))) {
        goto files_create_cleanup;
    }

    metadata = (log_metadata_t*)mmap(NULL, sizeof(log_metadata_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd_meta, 0);
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

    (void)ftruncate(fd_data, 0);  // zero out the file, probably
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
    handle->chunks.buffer = (_Atomic(chunk_info_t**))(chunk_info_t**)calloc(chunk_count, sizeof(chunk_info_t*));
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

bool mmlog_clean_chunks(log_handle_t* handle)
{
    // Cleans chunks from the ringbuffer if they are no longer in use
    if (!handle || !handle->metadata) {
        errno = EINVAL;
        return false;
    }
    chunk_buffer_t* chunks = &handle->chunks;

    while (true) {
        // Get the current tail and head pointers atomically
        chunk_info_t** tail_ptr = atomic_load(&chunks->tail);
        chunk_info_t** head_ptr = atomic_load(&chunks->head);

        // Check if buffer is empty
        if (tail_ptr == head_ptr) {
            // No chunks to clean
            return true;
        }

        // Get the chunk at the tail position
        chunk_info_t* tail_chunk = atomic_load((chunk_info_t * _Atomic*)tail_ptr);

        // Check if chunk can be cleaned
        if (!tail_chunk || tail_chunk == CHUNK_PENDING || atomic_load(&tail_chunk->ref_count) != 0) {
            break;  // Can't clean this chunk, exit
        }

        // Calculate the next tail position with wraparound
        ptrdiff_t tail_index = tail_ptr - chunks->buffer;
        ptrdiff_t next_index = (tail_index + 1) % chunks->capacity;
        chunk_info_t** next_tail_ptr = chunks->buffer + next_index;

        // Attempt to update the tail pointer atomically
        if (atomic_compare_exchange_strong(&chunks->tail, &tail_ptr, next_tail_ptr)) {
            // Success - clean up the chunk
            atomic_store((chunk_info_t * _Atomic*)tail_ptr, NULL);
            munmap(tail_chunk->mapping, tail_chunk->size);
            free(tail_chunk);
        } else {
            // Failed to update tail pointer, another thread changed it
            // Try again from the beginning
            continue;
        }
    }

    return true;
}

uint32_t mmlog_cross_count(uint64_t start, size_t size, uint32_t chunk_size)
{
    uint64_t end = start + size;

    // Calculate the chunk indices for the start and end positions
    uint64_t start_chunk_index = start / chunk_size;
    uint64_t end_chunk_index = end / chunk_size;
    return end_chunk_index - start_chunk_index;
}

static chunk_info_t* create_chunk_at_cursor(log_handle_t* handle, uint64_t cursor)
{
    chunk_info_t* chunk = (chunk_info_t*)calloc(1, sizeof(chunk_info_t));
    if (!chunk) {
        return NULL;
    }

    // Initialize the chunk for this cursor position
    chunk->start_offset = (cursor / handle->metadata->chunk_size) * handle->metadata->chunk_size;
    chunk->size = handle->metadata->chunk_size;
    chunk->mapping = mmap(NULL, chunk->size, PROT_READ | PROT_WRITE, MAP_SHARED, handle->data_fd, chunk->start_offset);

    if (MAP_FAILED == chunk->mapping) {
        free(chunk);
        return NULL;
    }

    atomic_store(&chunk->ref_count, 1);
    return chunk;
}

// Helper to check if cursor is within chunk
static bool is_cursor_in_chunk(const chunk_info_t* chunk, uint64_t cursor)
{
    return chunk && chunk != CHUNK_PENDING && chunk->start_offset <= cursor &&
           cursor < chunk->start_offset + chunk->size;
}

// Helper to wait for a pending chunk to become ready
static bool wait_for_pending_chunk(chunk_info_t* _Atomic* chunk_ptr)
{
    uint64_t start_time = ms_since_epoch_monotonic();
    uint64_t cur_time = start_time;

    while (cur_time - start_time < SLEEP_TIME_MAX_MS) {
        chunk_info_t* chunk = atomic_load(chunk_ptr);
        if (chunk != CHUNK_PENDING) {
            return true;
        }
        sched_yield();
        cur_time = ms_since_epoch_monotonic();
    }

    return false;
}

// Helper to lock the head pointer
static bool lock_head(chunk_buffer_t* chunks)
{
    bool expected = false;
    return atomic_compare_exchange_strong(&chunks->head_locked, &expected, true);
}

// Helper to unlock the head pointer
static void unlock_head(chunk_buffer_t* chunks)
{
    atomic_store(&chunks->head_locked, false);
}

// Helper function to handle uninitialized buffer
static chunk_info_t*
handle_uninitialized_buffer(log_handle_t* handle, chunk_buffer_t* chunks, chunk_info_t** head_ptr, uint64_t cursor)
{
    // Check if head_ptr is NULL
    if (!head_ptr) {
        errno = EINVAL;
        return NULL;
    }

    // Mark head as pending
    // TODO this cast is ugly and probably wrong?
    chunk_info_t* expected = NULL;
    if (!atomic_compare_exchange_strong((chunk_info_t * _Atomic*)head_ptr, &expected, CHUNK_PENDING)) {
        // CAS failed, someone else is updating - return NULL and let caller retry
        errno = EAGAIN;
        return NULL;
    }

    // Create new chunk at cursor
    chunk_info_t* new_chunk = create_chunk_at_cursor(handle, cursor);
    if (!new_chunk) {
        atomic_store((chunk_info_t * _Atomic*)head_ptr, NULL);  // Clear PENDING
        errno = ENOMEM;
        return NULL;
    }

    // Store new chunk and advance head pointer atomically
    atomic_store((chunk_info_t * _Atomic*)head_ptr, new_chunk);

    chunk_info_t** next_head_ptr = head_ptr + 1;
    if (next_head_ptr >= chunks->buffer + chunks->capacity) {
        next_head_ptr = chunks->buffer;  // Wrap around
    }
    atomic_store(&chunks->head, next_head_ptr);

    return new_chunk;
}

// Helper function to add a new chunk
static chunk_info_t* add_new_chunk(log_handle_t* handle,
                                   chunk_buffer_t* chunks,
                                   chunk_info_t** head_ptr,
                                   chunk_info_t** tail_ptr,
                                   uint64_t cursor)
{
    // Calculate next head position
    chunk_info_t** next_head_ptr = head_ptr + 1;
    if (next_head_ptr >= chunks->buffer + chunks->capacity) {
        next_head_ptr = chunks->buffer;  // Wrap around
    }

    // Check if buffer is full
    if (next_head_ptr == tail_ptr) {
        // Temporarily release lock to clean up
        unlock_head(chunks);
        mmlog_clean_chunks(handle);

        // Re-acquire lock and reload pointers
        if (!lock_head(chunks)) {
            errno = EAGAIN;
            return NULL;
        }

        // Recalculate next head position with updated pointers
        head_ptr = atomic_load(&chunks->head);
        tail_ptr = atomic_load(&chunks->tail);
        next_head_ptr = head_ptr + 1;
        if (next_head_ptr >= chunks->buffer + chunks->capacity) {
            next_head_ptr = chunks->buffer;
        }

        if (next_head_ptr == tail_ptr) {
            errno = ENOSPC;
            return NULL;
        }
    }

    // Mark next slot as pending
    chunk_info_t* expected = NULL;
    if (!atomic_compare_exchange_strong((chunk_info_t * _Atomic*)next_head_ptr, &expected, CHUNK_PENDING)) {
        errno = EAGAIN;
        return NULL;
    }

    // Create new chunk
    chunk_info_t* new_chunk = create_chunk_at_cursor(handle, cursor);
    if (!new_chunk) {
        atomic_store((chunk_info_t * _Atomic*)next_head_ptr, NULL);
        errno = ENOMEM;
        return NULL;
    }

    // Store new chunk
    atomic_store((chunk_info_t * _Atomic*)next_head_ptr, new_chunk);

    // Advance head pointer - if this fails, log a warning but return the chunk
    if (!atomic_compare_exchange_strong(&chunks->head, &head_ptr, next_head_ptr)) {
        // This is a rare race condition - the chunk is valid, but head wasn't updated
        // We should log this situation but return success
    }

    return new_chunk;
}

chunk_info_t* mmlog_rb_checkout(log_handle_t* handle, uint64_t cursor)
{
    // Validate inputs
    if (!handle || !handle->chunks.buffer) {
        errno = EINVAL;
        return NULL;
    }

    chunk_buffer_t* chunks = &handle->chunks;
    chunk_info_t* result = NULL;

    // Try to find an existing chunk with our cursor first (no lock needed)
    chunk_info_t** head_ptr = atomic_load(&chunks->head);
    if (head_ptr && *head_ptr != NULL && *head_ptr != CHUNK_PENDING) {
        chunk_info_t* head_chunk = atomic_load((chunk_info_t * _Atomic*)head_ptr);
        if (is_cursor_in_chunk(head_chunk, cursor)) {
            atomic_fetch_add(&head_chunk->ref_count, 1);
            return head_chunk;
        }
    }

    // Need to create or find a suitable chunk - acquire lock for the entire operation
    if (!lock_head(chunks)) {
        errno = EAGAIN;
        return NULL;
    }

    // Reload pointers after acquiring lock
    head_ptr = atomic_load(&chunks->head);
    chunk_info_t** tail_ptr = atomic_load(&chunks->tail);
    chunk_info_t* head_chunk = NULL;

    // Check for uninitialized buffer case
    if (head_ptr == NULL) {
        // Initialize head_ptr to point to the first element of the buffer if it's NULL
        head_ptr = chunks->buffer;
        atomic_store(&chunks->head, head_ptr);
        atomic_store(&chunks->tail, head_ptr);
    }

    if (atomic_load((_Atomic(chunk_info_t*)*)head_ptr) == NULL) {
        result = handle_uninitialized_buffer(handle, chunks, head_ptr, cursor);
        goto mmlog_rb_checkout_cleanup;
    }

    // Handle pending chunk case (wait for another thread to complete initialization)
    head_chunk = atomic_load((chunk_info_t * _Atomic*)head_ptr);
    if (head_chunk == CHUNK_PENDING) {
        // Temporarily release lock while waiting
        unlock_head(chunks);
        if (!wait_for_pending_chunk((chunk_info_t * _Atomic*)head_ptr)) {
            errno = ETIMEDOUT;
            return NULL;
        }

        // Re-acquire lock and reload head
        if (!lock_head(chunks)) {
            errno = EAGAIN;
            return NULL;
        }
        head_ptr = atomic_load(&chunks->head);
        head_chunk = atomic_load((chunk_info_t * _Atomic*)head_ptr);
        tail_ptr = atomic_load(&chunks->tail);
    }

    // Check again if head contains our cursor after potential changes
    if (is_cursor_in_chunk(head_chunk, cursor)) {
        atomic_fetch_add(&head_chunk->ref_count, 1);
        result = head_chunk;
        goto mmlog_rb_checkout_cleanup;
    }

    // We need to add a new chunk - calculate next head position
    result = add_new_chunk(handle, chunks, head_ptr, tail_ptr, cursor);

mmlog_rb_checkout_cleanup:
    unlock_head(chunks);
    return result;
}

static bool write_to_chunk(log_handle_t* handle, uint64_t cursor, const void* data, size_t size)
{
    chunk_info_t* chunk = mmlog_rb_checkout(handle, cursor);
    if (!chunk) {
        return false;  // Failed to checkout chunk
    }

    uint64_t chunk_offset = cursor - chunk->start_offset;
    memcpy((char*)chunk->mapping + chunk_offset, data, size);
    atomic_fetch_sub(&chunk->ref_count, 1);
    return true;
}

bool mmlog_insert(log_handle_t* handle, const void* data, size_t size)
{
    if (!handle || !data || size == 0) {
        errno = EINVAL;
        return false;
    }

    log_metadata_t* metadata = handle->metadata;

    uint64_t cursor = mmlog_checkout(handle, size);
    if (cursor == LOG_CURSOR_INVALID) {
        return false;
    }

    // With this, we have a contiguous range to write to, but it needs to be in-memory for us to map it.  Consider a few
    // scenarios
    // 1. The data fits in a chunk (although it may cross up to one chunk boundary)--check/add to ringbuffer
    // 2. The data crosses multiple chunks--just do a one-off mmap for this operation *OR* takes up exactly one chunk
    uint32_t crossings = mmlog_cross_count(cursor, size, metadata->chunk_size);
    if (crossings > 1 || (size == metadata->chunk_size && (cursor % metadata->chunk_size) == 0)) {
        // - If we have more than one crossing, it means that the intermediate chunk is going to be used solely for this
        // operation;
        //   so there is no point in using the ringbuffer--just map everything specifically for this operation
        // - If the span straddles a chunk in its entirety, then we also avoid using the ringbuffer
        // Rather than hassling with mmap, let's just do a direct write to memory
        return size == (size_t)pwrite(handle->data_fd, data, size, cursor);
    } else {
        // Handle 0 or 1 crossing by writing each portion to the appropriate chunk
        uint64_t current_cursor = cursor;
        size_t bytes_written = 0;

        while (bytes_written < size) {
            // Calculate chunk boundary
            uint64_t chunk_end = ((current_cursor / metadata->chunk_size) + 1) * metadata->chunk_size;

            // Calculate how much we can write in this chunk
            size_t bytes_to_write = size - bytes_written;
            if (current_cursor + bytes_to_write > chunk_end) {
                bytes_to_write = chunk_end - current_cursor;
            }

            // Write to this chunk
            if (!write_to_chunk(handle, current_cursor, (const char*)data + bytes_written, bytes_to_write)) {
                return false;
            }

            // Advance to next chunk
            current_cursor += bytes_to_write;
            bytes_written += bytes_to_write;
        }
    }

    // Check if we need to clean up chunks
    mmlog_clean_chunks(handle);

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
