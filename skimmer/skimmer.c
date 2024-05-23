#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file_path>\n", argv[0]);
        return 1;
    }
    const char *file_path = argv[1];

    // mmap the file instead
    int fd = open(file_path, O_RDONLY);
    if (fd == -1) {
        perror("Error opening file");
        return 1;
    }

    // Stat the file to get the size
    struct stat st;
    if (fstat(fd, &st) == -1) {
        perror("Error stating file");
        return 1;
    }
    unsigned char *data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        perror("Error mapping file");
        return 1;
    }

    // Can close the file now
    close(fd);

    // Now count the lines and bytes of the file
    size_t line = 1;
    size_t byte_offset = 0;

    // Print a header left-aligned to ten characters
    printf("%-10s %-10s %-10s %-10s\n", "Line", "Byte", "Page off.", "Page");

    for (byte_offset = 0; byte_offset < st.st_size; byte_offset++) {
        if (data[byte_offset] == '\n') {
            printf("%-10ld %-10ld %-10ld %-10ld\n", line, byte_offset, byte_offset % 4096, byte_offset / 4096);
            line++;
        }
    }

    // Cleanup I guess, not that it matters.
    munmap(data, st.st_size);
    return 0;
}

