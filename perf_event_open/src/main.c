#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <poll.h>
#include <unistd.h>
#include <wait.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>

#include <linux/perf_event.h>

// Structs
typedef struct perf_event_comm {
    struct perf_event_header header;
    uint32_t pid;
    uint32_t tid;
    char comm[];
} perf_event_comm;

typedef struct PerfEventConfig {
    int fds[1024];
    unsigned char* buffers[1024];
    size_t n;
} PerfEventConfig;

// Wrapper for the syscall
int perf_event_open(struct perf_event_attr* attr, pid_t pid, int cpu, int group_fd, unsigned long flags)
{
    return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

unsigned char* fd_to_buf(int fd)
{
    static const size_t page_size = 4096;
    size_t rb_data = 2 * 2 * page_size;
    size_t rb_size = page_size + rb_data;
    unsigned char *base_addr = mmap(NULL, rb_size + rb_data, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base_addr == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }
    unsigned char* second_map = mmap(base_addr + rb_data, rb_size, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED, fd, 0);
    if (second_map == MAP_FAILED) {
        perror("mmap");
        munmap(base_addr, rb_size + rb_data);
        return NULL;
    }
    unsigned char* first_map = mmap(base_addr, rb_size, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED, fd, 0);
    if (first_map == MAP_FAILED) {
        perror("mmap");
        munmap(base_addr, rb_size + rb_data);
        return NULL;
    }
    return base_addr;
}

PerfEventConfig* setup_perf_event(void)
{
    struct perf_event_attr attr = {
        .type = PERF_TYPE_SOFTWARE,
        .config = PERF_COUNT_SW_DUMMY,
        .size = sizeof(struct perf_event_attr),
        .sample_period = 1,
        .watermark = 1,
        .wakeup_watermark = 1,
        .disabled = 1,
        .comm = 1,
        .exclude_kernel = 1,
        .exclude_hv = 1,
    };

    // count the number of CPUs
    int num_cpus = get_nprocs();

    // This is a somewhat permissive implementation, but whatever
    PerfEventConfig* config = malloc(sizeof(PerfEventConfig));
    for (int i = 0; i < num_cpus; i++) {
        int fd = perf_event_open(&attr, 0, i, -1, 0);
        if (fd == -1) {
            continue;
        }
        unsigned char* buf = fd_to_buf(fd);
        if (buf == NULL) {
            close(fd);
            continue;
        }
        config->fds[i] = fd;
        config->buffers[i] = buf;
        config->n = i;
    }
    return config;
}

void enable_perf_events(PerfEventConfig* config)
{
    for (size_t i = 0; i < config->n; i++) {
        ioctl(config->fds[i], PERF_EVENT_IOC_ENABLE, 0);
    }
}

struct perf_event_header* header_from_buf(unsigned char* base)
{
    struct perf_event_mmap_page* metadata = (struct perf_event_mmap_page*)base;
    unsigned char* data_area = base + 4096;  // hardcoded
    uint64_t data_size = metadata->data_size;
    uint64_t data_head, data_tail;

    data_head = __atomic_load_n(&metadata->data_head, __ATOMIC_ACQUIRE);
    data_tail = metadata->data_tail;
    if (data_head == data_tail) {
        return NULL;
    }

    uint64_t offset = data_tail % data_size;
    struct perf_event_header* event = (struct perf_event_header*)(data_area + offset);

    if (data_head - data_tail < sizeof(*event)) {
        return NULL;  // not enough data for header
    }
    return event;
}

struct perf_event_header* get_next_event(unsigned char** bufs, int* fds, size_t n)
{
    // Accepts arrays of buffers and file descriptors
    struct pollfd pollfds[1024];
    for (size_t i = 0; i < n; i++) {
        pollfds[i].fd = fds[i];
        pollfds[i].events = POLLIN;
    }

    int ret = -1;
    errno = EINTR;
    while (ret == -1 && errno == EINTR) {
        ret = poll(pollfds, n, -1);
    }
    if (ret == -1) {
        perror("poll");
        return NULL;
    }

    for (size_t i = 0; i < n; i++) {
        if (pollfds[i].revents & POLLIN) {
            printf("Got an event on CPU %zu\n", i);
            return header_from_buf(bufs[i]);
        }
    }

    // Paradox?
    return NULL;
}

int main(void)
{
    PerfEventConfig* config = setup_perf_event();
    enable_perf_events(config);
    prctl(PR_SET_NAME, "poop", 0, 0, 0);

    struct perf_event_header* event = get_next_event(config->buffers, config->fds, config->n);
    struct perf_event_comm* comm = (struct perf_event_comm*)event;

    // Check if we got an event
    if (event == NULL) {
        printf("No event\n");
    } else {
        printf("Got event of type %d\n", event->type);
        printf("Comm: %s\n", comm->comm);
    }

    return 0;
}
