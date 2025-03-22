#pragma once

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
    _Atomic bool fill_gen;       // Fills new allocations with the current gen count (for debugging purposes)
    _Atomic uint64_t file_size;  // Current physical size of the data file
    _Atomic uint64_t cursor;     // Current append position
    _Atomic uint64_t gen_count;  // Generation count (incremented on each write)
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

// X macro for error codes
#define ERR_TABLE(X) \
    X(OK, "mmlog success") \
    X(CLOCK_GETTIMEOFDAY, "[ms_since_epoch] error in gettimeofday()") \
    X(FILES_CREATE_FTRUNCATE_MDATA, "[files_create] metadata ftruncate() failed") \
    X(FILES_CREATE_MMAP_MDATA, "[files_create] metadata mmap() failed") \
    X(FILES_CREATE_OPEN_DATA, "[files_create] data open() failed on data file") \
    X(FILES_CREATE_FTRUNCATE_DATA, "[files_create] data ftruncate() failed") \
    X(TRY_CHECK_FILE_READY_OPEN, "[try_check_file_ready] open() failed") \
    X(TRY_CHECK_FILE_READY_FSTAT, "[try_check_file_ready] fstat() failed") \
    X(FILES_OPEN_MDATA_READY_OPEN, "[files_open] metadata not ready (open)") \
    X(FILES_OPEN_MDATA_READY_FSTAT, "[files_open] metadata not ready (fstat)") \
    X(FILES_OPEN_MDATA_MMAP, "[files_open] metadata mmap() failed") \
    X(FILES_OPEN_MDATA_READY, "[files_open] metadata not ready") \
    X(FILES_OPEN_DATA_READY_OPEN, "[files_open] data not ready (open)") \
    X(FILES_OPEN_DATA_READY_FSTAT, "[files_open] data not ready (fstat)") \
    X(FILES_OPEN_DATA_MMAP, "[files_open] metadata mmap() failed") \
    X(FILES_OPEN_VERSION, "[files_open] incompatible metadata version") \
    X(FILES_OPEN_OR_CREATE_EINVAL, "[files_open_or_create] invalid arguments") \
    X(FILES_OPEN_OR_CREATE_SNPRINTF, "[files_open_or_create] failed to intern string") \
    X(MMLOG_OPEN_EINVAL, "[mmlog_open] invalid arguments") \
    X(MMLOG_OPEN_ENOMEM, "[mmlog_open] out of memory") \
    X(MMLOG_OPEN_OR_CREATE_EINVAL, "[mmlog_open_or_create] invalid arguments") \
    X(FILE_EXPAND_INNER_PANICKED, "[file_expand_inner] log is in a panic state") \
    X(FILE_EXPAND_INNER_GROW, "[file_expand_inner] ftruncate() failed") \
    X(FILE_EXPAND_INNER_LOCKED, "[file_expand_inner] failed to acquire lock") \
    X(MMLOG_CHECKOUT_EINVAL, "[mmlog_checkout] invalid arguments") \
    X(MMLOG_CLEAN_CHUNKS_EINVAL, "[mmlog_clean_chunks] invalid arguments") \
    X(CREATE_CHUNK_AT_CURSOR_ENOMEM, "[create_chunk_at_cursor] out of memory") \
    X(CREATE_CHUNK_AT_CURSOR_MMAP, "[create_chunk_at_cursor] mmap() failed") \
    X(ADD_NEW_CHUNK_EINVALID, "[add_new_chunk] invalid arguments") \
    X(ADD_NEW_CHUNK_EWAIT, "[add_new_chunk] ringbuffer is full") \
    X(MMLOG_RB_CHECKOUT_EINVAL, "[mmlog_rb_checkout] invalid arguments") \
    X(MMLOG_RB_CHECKOUT_NOLOCK, "[mmlog_rb_checkout] failed to acquire head lock") \
    X(WRITE_TO_CHUNK_ECHUNK, "[write_to_chunk] failed to checkout chunk") \
    X(MMLOG_INSERT_EINVAL, "[mmlog_insert] invalid arguments") \
    X(MMLOG_INSERT_PWRITE, "[mmlog_insert] pwrite() failed") \
    X(MMLOG_TRIM_EINVAL, "[mmlog_trim] invalid arguments") \
    X(MMLOG_TRIM_FTRUNCATE, "[mmlog_trim] ftruncate() failed") \
    X(_LENGTH, "UNKNOWN ERROR")

// Error codes
#define ERR_CODE(code, str) MMLOG_ERR_##code,
typedef enum {
    ERR_TABLE(ERR_CODE)  // Generate error codes from the table
} mmlog_err_t;
#undef ERR_CODE

// Error messages
#define ERR_MSG(code, str) [MMLOG_ERR_##code] = str,
static const char* mmlog_err_msg[] = {
    ERR_TABLE(ERR_MSG)  // Generate error messages from the table
};
#undef ERR_MSG

const char * mmlog_strerror(mmlog_err_t err)
{
    err = (err < 0 || err >= MMLOG_ERR__LENGTH) ? MMLOG_ERR__LENGTH : err;  // Clamp to valid range
    return mmlog_err_msg[err];
}

mmlog_err_t mmlog_errno = MMLOG_ERR_OK;
const char * mmlog_strerror_cur() {
    return mmlog_strerror(mmlog_errno);
}

int64_t ms_since_epoch_monotonic(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        mmlog_errno = MMLOG_ERR_CLOCK_GETTIMEOFDAY;
        return -1;  // I mean, sure, but will anybody realistically ever check?
    }
    return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
}

static inline bool hot_wait_for_cond(bool (*fun)(void*), void* arg, int64_t timeout_ms)
{
    int64_t start_time = ms_since_epoch_monotonic();
    int64_t cur_time = start_time;
    while (cur_time - start_time < timeout_ms) {
        if (fun(arg)) {
            return true;
        }
        sched_yield();
        cur_time = ms_since_epoch_monotonic();
    }
    return false;
}

bool files_create(int fd_meta, const char* filename, uint32_t chunk_size, log_handle_t* handle)
{
    log_metadata_t* metadata = NULL;
    int fd_data = -1;
    mmlog_errno = MMLOG_ERR_OK;
    if (-1 == ftruncate(fd_meta, sizeof(log_metadata_t))) {
        mmlog_errno = MMLOG_ERR_FILES_CREATE_FTRUNCATE_MDATA;
        goto files_create_cleanup;
    }

    metadata = (log_metadata_t*)mmap(NULL, sizeof(log_metadata_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd_meta, 0);
    if (MAP_FAILED == metadata) {
        mmlog_errno = MMLOG_ERR_FILES_CREATE_MMAP_MDATA;
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
        mmlog_errno = MMLOG_ERR_FILES_CREATE_OPEN_DATA;
        goto files_create_cleanup;
    }

    // Zero + init the data file and init the metadata file
    if (-1 == ftruncate(fd_data, 0) || -1 == ftruncate(fd_data, chunk_size)) {
        mmlog_errno = MMLOG_ERR_FILES_CREATE_FTRUNCATE_DATA;
        goto files_create_cleanup;
    }

    handle->metadata = metadata;
    handle->data_fd = fd_data;
    handle->metadata_fd = fd_meta;
    atomic_store(&metadata->gen_count, 0);
    atomic_store(&metadata->file_size, chunk_size);
    atomic_store(&metadata->is_ready, true);
    return true;

files_create_cleanup:
    if (metadata && metadata != MAP_FAILED) {
        if (-1 == munmap(metadata, sizeof(log_metadata_t))) {
            // Nothing to do here
        }
    }
    if (-1 != fd_data) {
        close(fd_data);
    }
    return false;
}

typedef struct {
    int* fd;
    const char* filename;
    size_t size;
} try_check_file_ready_args_t;

static inline bool try_check_file_ready(void* args)
{
    try_check_file_ready_args_t* m = (try_check_file_ready_args_t*)args;
    int* fd = m->fd;
    size_t size = m->size;
    const char* filename = m->filename;
    if (-1 == *fd) {
        *fd = open(filename, O_RDWR);
        if (-1 == *fd) {
            mmlog_errno = MMLOG_ERR_TRY_CHECK_FILE_READY_OPEN;
            return false;
        }
    }
    struct stat st;
    if (0 != fstat(*fd, &st) || (size_t)st.st_size < size) {
        mmlog_errno = MMLOG_ERR_TRY_CHECK_FILE_READY_FSTAT;
        return false;
    }
    return true;
}

static inline bool try_check_metadata_ready(void* args)
{
    log_metadata_t* metadata = (log_metadata_t*)args;
    return atomic_load(&metadata->is_ready);
}

bool files_open(const char* filename, const char* meta_filename, log_handle_t* handle)
{
    mmlog_errno = MMLOG_ERR_OK;
    int fd_meta = -1;
    int fd_data = -1;
    log_metadata_t* metadata = handle->metadata;

    // Before we mmap the metadata, check the file size.  If it's nonzero, but less than the size of the metadata, it's
    // probable that we have an incompatible metadata version--so bail out
    try_check_file_ready_args_t fd_meta_args = {&fd_meta, meta_filename, sizeof(log_metadata_t)};
    try_check_file_ready_args_t fd_data_args = {&fd_data, filename, 0};
    if (!hot_wait_for_cond(try_check_file_ready, &fd_meta_args, SLEEP_TIME_MAX_MS)) {
        if (mmlog_errno == MMLOG_ERR_TRY_CHECK_FILE_READY_OPEN) {
            mmlog_errno = MMLOG_ERR_FILES_OPEN_MDATA_READY_OPEN;
        } else if (mmlog_errno == MMLOG_ERR_TRY_CHECK_FILE_READY_FSTAT) {
            mmlog_errno = MMLOG_ERR_FILES_OPEN_MDATA_READY_FSTAT;
        }
        goto files_open_cleanup;
    }

    // Nothing is valid until we've mapped the metadata (above check ensures the metadata file is at least as big as we
    // need)
    metadata = (log_metadata_t*)mmap(NULL, sizeof(log_metadata_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd_meta, 0);
    fd_data_args.size = metadata->chunk_size;  // Set the size for the data file check
    if (MAP_FAILED == metadata) {
        mmlog_errno = MMLOG_ERR_FILES_OPEN_MDATA_MMAP;
        goto files_open_cleanup;
    }

    // Sit in a try-sleep loop for checking readiness
    if (!hot_wait_for_cond(try_check_metadata_ready, metadata, SLEEP_TIME_MAX_MS)) {
        mmlog_errno = MMLOG_ERR_FILES_OPEN_MDATA_READY;
        goto files_open_cleanup;
    }

    // If the version is not compatible, we can't use this log
    if (metadata->version != MMLOG_VERSION) {
        mmlog_errno = MMLOG_ERR_FILES_OPEN_VERSION;
        goto files_open_cleanup;
    }

    // Finally, we have to open the data file
    if (!hot_wait_for_cond(try_check_file_ready, &fd_data_args, SLEEP_TIME_MAX_MS)) {
        if (mmlog_errno == MMLOG_ERR_TRY_CHECK_FILE_READY_OPEN) {
            mmlog_errno = MMLOG_ERR_FILES_OPEN_DATA_READY_OPEN;
        } else if (mmlog_errno == MMLOG_ERR_TRY_CHECK_FILE_READY_FSTAT) {
            mmlog_errno = MMLOG_ERR_FILES_OPEN_DATA_READY_FSTAT;
        }
        goto files_open_cleanup;
    }

    handle->metadata_fd = fd_meta;
    handle->data_fd = fd_data;
    handle->metadata = metadata;

    return true;

files_open_cleanup:
    if (metadata && MAP_FAILED != metadata) {
        munmap(metadata, sizeof(log_metadata_t));
    }
    if (-1 != fd_data) {
        close(fd_data);
    }
    if (-1 != fd_meta) {
        close(fd_meta);
    }
    return false;
}

bool files_open_or_create(const char* filename, uint32_t chunk_size, log_handle_t* handle)
{
    mmlog_errno = MMLOG_ERR_OK;
    if (!filename || !*filename || chunk_size == 0 || chunk_size % LOG_PAGE_SIZE != 0 || !handle) {
        mmlog_errno = MMLOG_ERR_FILES_OPEN_OR_CREATE_EINVAL;
        return false;
    }

    static const char* suffix = ".mmlog";
    size_t meta_filename_len = strlen(filename) + strlen(suffix) + 1;
    char* meta_filename = (char*)malloc(meta_filename_len);
    int fd_meta = -1;
    if (!meta_filename) {
        mmlog_errno = MMLOG_ERR_FILES_OPEN_OR_CREATE_EINVAL;
        goto files_open_or_create_cleanup;
    }

    if (snprintf(meta_filename, meta_filename_len, "%s%s", filename, suffix) < 0) {
        mmlog_errno = MMLOG_ERR_FILES_OPEN_OR_CREATE_SNPRINTF;
        goto files_open_or_create_cleanup;
    }

    // Try to create metadata file (exclusive create)
    // NOTE: we never `unlink()` this file, so some error conditions can cause an orphaned metadata file.
    //       TODO: can we do better?  Difficult if ftruncate fails
    fd_meta = open(meta_filename, O_RDWR | O_CREAT | O_EXCL, 0644);
    if (-1 == fd_meta) {
        if (!files_open(filename, meta_filename, handle)) {
            // callee sets errno
            goto files_open_or_create_cleanup;
        }
    } else {
        if (!files_create(fd_meta, filename, chunk_size, handle)) {
            // callee sets errno
            goto files_open_or_create_cleanup;
        }
    }

    return true;

files_open_or_create_cleanup:
    free(meta_filename);
    if (-1 != fd_meta) {
        close(fd_meta);
    }
    return false;
}

log_handle_t* mmlog_open(const char* filename, size_t chunk_size, uint32_t chunk_count)
{
    mmlog_errno = MMLOG_ERR_OK;
    // chunks are page multiples, and we need at least 2 chunks
    if (chunk_size % LOG_PAGE_SIZE != 0 || chunk_size == 0 || chunk_count < 2) {
        mmlog_errno = MMLOG_ERR_MMLOG_OPEN_EINVAL;
        return NULL;
    }

    // Allocate handle
    log_handle_t* handle = (log_handle_t*)calloc(1, sizeof(log_handle_t));
    if (!handle) {
        mmlog_errno = MMLOG_ERR_MMLOG_OPEN_ENOMEM;
        return NULL;
    }

    // Allocate chunk buffer
    handle->chunks.capacity = chunk_count;
    handle->chunks.buffer = (_Atomic(chunk_info_t**))(chunk_info_t**)calloc(chunk_count, sizeof(chunk_info_t*));
    if (!handle->chunks.buffer) {
        mmlog_errno = MMLOG_ERR_MMLOG_OPEN_ENOMEM;
        free(handle);
        return NULL;
    }

    for (uint32_t i = 0; i < chunk_count; i++) {
        handle->chunks.buffer[i] = NULL;
    }

    if (!files_open_or_create(filename, chunk_size, handle)) {
        // callee sets errno
        free(handle->chunks.buffer);
        free(handle);
        return NULL;
    }

    return handle;
}

typedef struct {
    log_handle_t* handle;
    uint64_t end;
} file_expand_args_t;

static inline bool file_expand_inner(void* arg)
{
    mmlog_errno = MMLOG_ERR_OK;

    file_expand_args_t* args = (file_expand_args_t*)arg;
    log_handle_t* handle = args->handle;
    uint64_t end = args->end;
    log_metadata_t* metadata = handle->metadata;

    if (atomic_load(&metadata->is_panicked)) {
        // Unfortunate, but we'll probably end up retrying in this condition
        // TODO fence this properly or create an early escape enum or something
        mmlog_errno = MMLOG_ERR_FILE_EXPAND_INNER_PANICKED;
        return false;
    }

    bool expected = false;
    if (atomic_compare_exchange_strong(&metadata->is_locked, &expected, true)) {
        uint64_t file_size = atomic_load(&metadata->file_size);
        if (file_size < end) {
            // Need to expand
            uint64_t new_size = ALIGN(end, metadata->chunk_size);
            if (-1 == ftruncate(handle->data_fd, new_size)) {
                // Whoa, we can't ftruncate!  Everything sucks!
                atomic_store(&metadata->is_panicked, true);
                atomic_store(&metadata->is_locked, false);
                mmlog_errno = MMLOG_ERR_FILE_EXPAND_INNER_GROW;
                return true;  // Bails out, but we we need to check in the caller
            }
            //

            // Expanded, update metadata
            atomic_store(&metadata->file_size, new_size);
            atomic_store(&metadata->is_locked, false);
            file_size = new_size;
            return true;
        }

        // release the lock--filesize is fine
        atomic_store(&metadata->is_locked, false);
        return true;
    }

    // Couldn't take the lock, so retry
    mmlog_errno = MMLOG_ERR_FILE_EXPAND_INNER_LOCKED;
    return false;
}

bool data_file_expand(log_handle_t* handle, uint64_t end)
{
    mmlog_errno = MMLOG_ERR_OK;
    file_expand_args_t args = {handle, end};
    if (hot_wait_for_cond(file_expand_inner, &args, SLEEP_TIME_MAX_MS)) {
        // callee sets errno
        return !atomic_load(&handle->metadata->is_panicked);
    }
    return false;
}

uint64_t mmlog_checkout(log_handle_t* handle, size_t size)
{
    // Gets the start position for the requested write operation
    // Will increase the capacity of the file if needed, but does not map the new pages
    mmlog_errno = MMLOG_ERR_OK;
    if (!handle || !handle->metadata) {
        mmlog_errno = MMLOG_ERR_MMLOG_CHECKOUT_EINVAL;
        return LOG_CURSOR_INVALID;
    }

    log_metadata_t* metadata = handle->metadata;
    uint64_t start = atomic_fetch_add(&metadata->cursor, size);
    uint64_t end = start + size;
    uint64_t file_size = atomic_load(&metadata->file_size);

    if (end > file_size) {
        if (!data_file_expand(handle, end)) {
            // callee sets errno
            return LOG_CURSOR_INVALID;
        }
    }
    return start;
}

bool mmlog_clean_chunks(chunk_buffer_t* chunks)
{
    // Attempts to clean up any unused chunks in the ring buffer, starting from the tail
    // Chunks are unusued if they are not the head and their refcount is 0
    // This type of property checking is done in a few steps
    // 1. Atomic acquisition of the current tail
    // 2. Verification that this snapshot has certain properties
    // 3. "Take" the tail atomically via a CAS type operation
    // 4. If successful, clean it up and repeat.
    // 5. If not, then repeat anyway
    // 6. Stop if the current tail cannot be cleaned (e.g., it is the head or it has a nonzero refcount)
    mmlog_errno = MMLOG_ERR_OK;
    if (!chunks || !chunks->buffer) {
        mmlog_errno = MMLOG_ERR_MMLOG_CLEAN_CHUNKS_EINVAL;
        return false;  // Invalid chunk buffer
    }

    while (true) {
        // Get the current tail and head pointers atomically
        chunk_info_t** tail_ptr = atomic_load(&chunks->tail);
        chunk_info_t** head_ptr = atomic_load(&chunks->head);

        // If there's no tail, we're done here
        if (!tail_ptr) {
            break;  // No tail to clean
        }
        chunk_info_t* tail_chunk = *tail_ptr;

        // No matter what, if head==tail we don't have to clean anything up
        if (tail_ptr == head_ptr) {
            // No chunks to clean
            return true;
        }

        // Get the chunk at the tail position
        if (!tail_chunk || tail_chunk == CHUNK_PENDING) {
            break;  // No chunk to clean
        }

        // Check if chunk can be cleaned
        if (!tail_chunk->is_untracked && tail_chunk->ref_count > 0) {
            break;  // Also no chunk to clean
        }

        // Calculate the next tail position with wraparound
        ptrdiff_t tail_index = tail_ptr - chunks->buffer;
        ptrdiff_t next_index = (tail_index + 1) % chunks->capacity;
        chunk_info_t** next_tail_ptr = chunks->buffer + next_index;

        // Attempt to update the tail pointer atomically
        if (atomic_compare_exchange_strong(&chunks->tail, &tail_ptr, next_tail_ptr)) {
            // Success - clean up the chunk
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
    mmlog_errno = MMLOG_ERR_OK;
    chunk_info_t* chunk = (chunk_info_t*)calloc(1, sizeof(chunk_info_t));
    if (!chunk) {
        mmlog_errno = MMLOG_ERR_CREATE_CHUNK_AT_CURSOR_ENOMEM;
        return NULL;
    }

    // Initialize the chunk for this cursor position
    chunk->start_offset = (cursor / handle->metadata->chunk_size) * handle->metadata->chunk_size;
    chunk->size = handle->metadata->chunk_size;
    chunk->mapping = mmap(NULL, chunk->size, PROT_READ | PROT_WRITE, MAP_SHARED, handle->data_fd, chunk->start_offset);

    if (MAP_FAILED == chunk->mapping) {
        mmlog_errno = MMLOG_ERR_CREATE_CHUNK_AT_CURSOR_MMAP;
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

static inline bool lock_head_inner(void* args)
{
    chunk_buffer_t* chunks = (chunk_buffer_t*)args;
    bool expected = false;
    return atomic_compare_exchange_strong(&chunks->head_locked, &expected, true);
}

// Helper to lock the head pointer
static inline bool lock_head(chunk_buffer_t* chunks)
{
    return hot_wait_for_cond(lock_head_inner, chunks, SLEEP_TIME_MAX_MS);
}

// Helper to unlock the head pointer
static void unlock_head(chunk_buffer_t* chunks)
{
    atomic_store(&chunks->head_locked, false);
}

// Fiddly arithmetic goes into a helper
static inline chunk_info_t** get_next_head_ptr(chunk_buffer_t* chunks, chunk_info_t** head_ptr)
{
    return chunks->buffer + ((head_ptr - chunks->buffer + 1) % chunks->capacity);
}

// Helper function to add a new chunk
static chunk_info_t* add_new_chunk(log_handle_t* handle,
                                   chunk_buffer_t* chunks,
                                   chunk_info_t** head_ptr,
                                   chunk_info_t** tail_ptr,
                                   uint64_t cursor)
{
    mmlog_errno = MMLOG_ERR_OK;

    // Check if head_ptr is NULL
    if (!head_ptr) {
        mmlog_errno = MMLOG_ERR_ADD_NEW_CHUNK_EINVALID;
        return NULL;
    }

    // Calculate next head position
    chunk_info_t** next_head_ptr = get_next_head_ptr(chunks, head_ptr);
    if (next_head_ptr >= chunks->buffer + chunks->capacity) {
        next_head_ptr = chunks->buffer;  // Wrap around
    }

    // Check if buffer is full, retry _one_ time
    if (next_head_ptr == tail_ptr) {
        mmlog_clean_chunks(&handle->chunks);

        // Recalculate next head position with updated pointers
        head_ptr = atomic_load(&chunks->head);
        tail_ptr = atomic_load(&chunks->tail);
        next_head_ptr = get_next_head_ptr(chunks, head_ptr);

        if (next_head_ptr == tail_ptr) {
            // Still full after cleaning, so we can't add a new chunk
            mmlog_errno = MMLOG_ERR_ADD_NEW_CHUNK_EWAIT;
            return NULL;
        }
    }

    // Create new chunk
    chunk_info_t* new_chunk = create_chunk_at_cursor(handle, cursor);
    if (!new_chunk) {
        // Unmark the pending chunk; I guess this might create churn as other writers try the same thing
        // TODO: probably a global failure state, like our panic state
        // callee sets errno
        atomic_store((chunk_info_t * _Atomic*)next_head_ptr, NULL);
        return NULL;
    }

    // Store new chunk
    atomic_store((chunk_info_t * _Atomic*)next_head_ptr, new_chunk);

    // Advance head pointer
    if (!atomic_compare_exchange_strong(&chunks->head, &head_ptr, next_head_ptr)) {
        // This should _never_ happen because the caller has the head lock
    }

    return new_chunk;
}

chunk_info_t* mmlog_rb_checkout(log_handle_t* handle, uint64_t cursor)
{
    mmlog_errno = MMLOG_ERR_OK;

    // Validate inputs
    if (!handle || !handle->chunks.buffer) {
        mmlog_errno = MMLOG_ERR_MMLOG_RB_CHECKOUT_EINVAL;
        return NULL;
    }

    // Need to create or find a suitable chunk - acquire lock for the entire operation
    chunk_buffer_t* chunks = &handle->chunks;
    if (!lock_head(chunks)) {
        mmlog_errno = MMLOG_ERR_MMLOG_RB_CHECKOUT_NOLOCK;
        return NULL;
    }
    chunk_info_t** head_ptr = atomic_load(&chunks->head);
    chunk_info_t** tail_ptr = atomic_load(&chunks->tail);
    if (!head_ptr) {
        head_ptr = chunks->buffer;  // Initialize head pointer if NULL
    }
    chunk_info_t* head_chunk = *head_ptr;

    // If we fit inside of the head, we're done
    if (head_chunk && is_cursor_in_chunk(head_chunk, cursor)) {
        atomic_fetch_add(&head_chunk->ref_count, 1);
        unlock_head(chunks);
        return head_chunk;
    }

    head_chunk = add_new_chunk(handle, chunks, head_ptr, tail_ptr, cursor);
    if (!head_chunk) {
        // callee sets errno
        unlock_head(chunks);
        return NULL;  // Failed to add new chunk
    }
    unlock_head(chunks);
    atomic_fetch_add(&head_chunk->ref_count, 1);
    return head_chunk;
}

static bool write_to_chunk(log_handle_t* handle, uint64_t cursor, const void* data, size_t size)
{
    mmlog_errno = MMLOG_ERR_OK;
    chunk_info_t* chunk = mmlog_rb_checkout(handle, cursor);
    if (!chunk) {
        mmlog_errno = MMLOG_ERR_WRITE_TO_CHUNK_ECHUNK;
        return false;  // Failed to checkout chunk
    }

    uint64_t chunk_offset = cursor - chunk->start_offset;
    memcpy((char*)chunk->mapping + chunk_offset, data, size);
    atomic_fetch_add(&handle->metadata->gen_count, 1);  // Increment generation count
    atomic_fetch_sub(&chunk->ref_count, 1);
    return true;
}

bool mmlog_insert(log_handle_t* handle, const void* data, size_t size)
{
    mmlog_errno = MMLOG_ERR_OK;

    if (!handle || !data || size == 0) {
        mmlog_errno = MMLOG_ERR_MMLOG_INSERT_EINVAL;
        return false;
    }

    log_metadata_t* metadata = handle->metadata;

    uint64_t cursor = mmlog_checkout(handle, size);
    if (LOG_CURSOR_INVALID == cursor) {
        // callee sets errno
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
        // TODO signals?
        if (size != (size_t)pwrite(handle->data_fd, data, size, cursor)) {
            mmlog_errno = MMLOG_ERR_MMLOG_INSERT_PWRITE;
            return false;  // Failed to write to chunk
        }
        return true;
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
                // callee sets errno
                return false;
            }

            // Advance to next chunk
            current_cursor += bytes_to_write;
            bytes_written += bytes_to_write;
        }
    }

    // Check if we need to clean up chunks
    mmlog_clean_chunks(&handle->chunks);

    return true;
}

void mmlog_trim(log_handle_t* handle)
{
    mmlog_errno = MMLOG_ERR_OK;

    // Trims the log file to the current cursor position
    if (!handle || !handle->metadata) {
        mmlog_errno = MMLOG_ERR_MMLOG_TRIM_EINVAL;
        return;
    }

    log_metadata_t* metadata = handle->metadata;
    uint64_t cursor = atomic_load(&metadata->cursor);
    if (-1 == ftruncate(handle->data_fd, cursor)) {
        // Technically this is a failure, but I have no idea what to do since this is like an optional validation nobody
        // asked for
        mmlog_errno = MMLOG_ERR_MMLOG_TRIM_FTRUNCATE;
    }
}

#undef SLEEP_TIME_MAX_MS
