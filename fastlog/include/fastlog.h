#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <sched.h>

// Constants
#define LOG_PAGE_SIZE 4096   // Fixed page size
#define SLEEP_TIME_MAX_MS 10 // Maximum sleep time in milliseconds (4 + 4 + a bit)
#define MMLOG_VERSION 1      // Current version of the log format

// Alignment macro
#define ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))

typedef struct {
    _Atomic uint32_t version;      // Format version number
    _Atomic bool is_ready;         // Initialization flag (true when fully initialized)
    _Atomic bool is_locked;        // Lock flag for file extension
    _Atomic uint64_t file_size;    // Current physical size of the data file
    _Atomic uint64_t cursor;       // Current append position
    _Atomic uint32_t page_size;    // Fixed page size value
    _Atomic uint32_t chunk_size;   // Size of chunks (multiple of page_size)
} log_metadata_t;

// Chunk tracking (returned at checkout time)
typedef struct {
    void *mapping;                    // Pointer to mapped memory
    uint64_t start_offset;            // Start offset in file
    uint64_t size;                    // Size of this chunk
    uint32_t generation;              // Generation when this chunk was created
    _Atomic uint32_t ref_count;       // Number of active users
} chunk_info_t;

// Process-local state
typedef struct {
    int metadata_fd;                  // File descriptor for metadata
    int data_fd;                      // File descriptor for data
    log_metadata_t *metadata;         // Pointer to mapped metadata
    uint32_t current_generation;      // Current mapping generation for this process
} log_handle_t;

// Range returned from checkout operation
typedef struct {
    uint64_t start;                   // Start position in log
    uint64_t end;                     // End position in log
    chunk_info_t *chunk;              // Pointer to the chunk containing this range
    uint64_t chunk_offset;            // Offset into the chunk where this range begins
} log_range_t;

int64_t ms_since_epoch_monotonic(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return -1; // I mean, sure, but will anybody realistically ever check?
    }
    return (int64_t)ts.tv_sec * 1000 + (int64_t)ts.tv_nsec / 1000000;
}

bool files_create(int fd_meta, const char *filename, uint32_t chunk_size, log_handle_t *handle) {
    ftruncate(fd_meta, sizeof(log_metadata_t));
    log_metadata_t *metadata = mmap(NULL, sizeof(log_metadata_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd_meta, 0);
    int fd_data = -1;
    if (MAP_FAILED == metadata) {
        goto files_create_cleanup;
    }

    atomic_store(&metadata->version, MMLOG_VERSION);
    atomic_store(&metadata->page_size, LOG_PAGE_SIZE);
    atomic_store(&metadata->chunk_size, chunk_size);

    // We're going to try and open the data file, but it's a bit tricky.
    // 1. if it doesn't exist--great!
    // 2. if it does exist, it's hard to figure out where to put the cursor, and we don't want to corrupt incoming binary data.
    // For this implementation, just clobber the file!
    fd_data = open(filename, O_RDWR | O_CREAT, 0644);
    if (-1 == fd_data) {
        goto files_create_cleanup;
    }

    ftruncate(fd_data, 0); // zero out the file
    if (-1 == ftruncate(fd_data, metadata->chunk_size)) {
        goto files_create_cleanup;
    }

    atomic_store(&metadata->file_size, chunk_size);
    atomic_store(&metadata->is_ready, true);
    handle->metadata = metadata;
    return true;

files_create_cleanup:
    munmap(metadata, sizeof(log_metadata_t));
    close(fd_meta);
    close(fd_data);
    return false;
}

bool files_open(const char *filename, const char *meta_filename, log_handle_t *handle) {
   int fd_meta = open(meta_filename, O_RDWR);
   log_metadata_t *metadata = NULL;
   if (-1 == fd_meta) {
       return false;
   }

   // Before we mmap the metadata, check the file size.  If it's nonzero, but less than the size of the metadata, it's probable that we have
   // an incompatable metadata version--so bail out
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
   metadata = mmap(NULL, sizeof(log_metadata_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd_meta, 0);
   if (MAP_FAILED == metadata || metadata->version != MMLOG_VERSION) {
        goto files_open_cleanup;
   }

   // Sit in a try-sleep loop for checking readiness
   int64_t start_time = ms_since_epoch_monotonic();
   int64_t cur_time =  start_time;
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

bool files_open_or_create(const char *filename, uint32_t chunk_size, log_handle_t *handle) {
    static const char *suffix = ".mmlog";
    size_t meta_filename_len = strlen(filename) + strlen(suffix) + 1;
    char* meta_filename = malloc(meta_filename_len);
    if (!meta_filename) {
        return false;
    }

    if (snprintf(meta_filename, meta_filename_len, "%s.mmlog", filename) < 0) {
        free(meta_filename);
        return false;
    }

    // Try to create metadata file (exclusive create)
    int fd_meta = open(meta_filename, O_RDWR | O_CREAT | O_EXCL, 0644);
    if (-1 == fd_meta) {
        if (!files_open(filename, meta_filename, handle)) {
            free(meta_filename);
            return false;
        }
    } else {
        if (!files_create(fd_meta, filename,chunk_size, handle)) {
            free(meta_filename);
            close(fd_meta);
            return false;
        }
    }

    free(meta_filename);
    return true;
}

log_handle_t* mmlog_open(const char* filename, size_t chunk_size) {
    // Validate chunk size (must be multiple of page size)
    if (chunk_size % LOG_PAGE_SIZE != 0 || chunk_size == 0) {
        errno = EINVAL;
        return NULL;
    }

    // Allocate handle
    log_handle_t* handle = calloc(1, sizeof(log_handle_t));
    if (!handle) {
        return NULL;
    }

    if (!files_open_or_create(filename, chunk_size, handle)) {
        free(handle);
        return NULL;
    }

    return handle;
}
