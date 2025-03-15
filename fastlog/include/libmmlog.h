// The purpose of this file is to provide FFI bindings
#pragma once
#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
#define EXTERN_C extern "C"
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define EXTERN_C extern
#endif

typedef struct log_metadata_t log_metadata_t;
typedef struct chunk_info_t chunk_info_t;
typedef struct log_handle_t log_handle_t;
typedef struct log_range_t log_range_t;

EXTERN_C log_handle_t* mmlog_open(const char* filename, size_t chunk_size, uint32_t max_chunks);
EXTERN_C bool mmlog_insert(log_handle_t* handle, const void* data, size_t size);
EXTERN_C void mmlog_trim(log_handle_t* handle);
#undef EXTERN_C
