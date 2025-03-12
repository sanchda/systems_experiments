#include "fastlog.h"
#include <x86intrin.h> // For __rdtsc()

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    log_context_t *log = init_log("vmread_trace.bin", 1024 * 1024);
    if (!log) {
        perror("Failed to initialize log");
        return 1;
    }

    // Record process_vm_readv call
    uint64_t generation = 1;
    uint64_t tsc = __rdtsc();
    uint64_t address = 0x7fff1234;
    uint64_t size = 4096;

    if (!append_record(log, generation, tsc, address, size)) {
        perror("Failed to append record");
        close_log(log);
        return 1;
    }

    // Read back all records
    log_iterator_t *iter = create_iterator(log);
    log_record_t record;

    while (next_record(iter, &record)) {
        printf("Gen: %lu, TSC: %lu, Addr: 0x%lx, Size: %lu\n",
               record.generation, record.tsc, record.address, record.size);
    }

    free_iterator(iter);
    close_log(log);
    return 0;
}
