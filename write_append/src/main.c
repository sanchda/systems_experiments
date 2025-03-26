#include <iostream>
#include <chrono>
#include <string>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <stdlib.h>
#include <libaio.h>

enum class AppendMethod {
    WRITE_APPEND,    // O_APPEND with write()
    WRITEV_APPEND,   // writev() with O_APPEND
    FWRITE_APPEND,   // FILE streams with "a" mode
    DIRECT_APPEND,   // O_DIRECT + O_APPEND
    AIO_APPEND       // Linux AIO with O_APPEND
};

class FileAppender {
private:
    AppendMethod method_;
    int fd_ = -1;
    FILE* file_ = nullptr;
    io_context_t aio_ctx_ = 0;
    void* aligned_buffer_ = nullptr;
    size_t aligned_buffer_size_ = 0;

public:
    FileAppender(AppendMethod method) : method_(method) {}

    ~FileAppender() {
        Close();
    }

    bool Open(const std::string& filename) {
        switch (method_) {
            case AppendMethod::WRITE_APPEND:
            case AppendMethod::WRITEV_APPEND:
                fd_ = open(filename.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
                return fd_ != -1;

            case AppendMethod::FWRITE_APPEND:
                file_ = fopen(filename.c_str(), "a");
                return file_ != nullptr;

            case AppendMethod::DIRECT_APPEND:
                fd_ = open(filename.c_str(), O_WRONLY | O_APPEND | O_DIRECT | O_CREAT, 0644);
                if (fd_ != -1) {
                    // Allocate aligned buffer for direct I/O
                    aligned_buffer_size_ = 4096; // Typical page size
                    posix_memalign(&aligned_buffer_, aligned_buffer_size_, aligned_buffer_size_);
                }
                return fd_ != -1 && aligned_buffer_ != nullptr;

            case AppendMethod::AIO_APPEND:
                fd_ = open(filename.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
                memset(&aio_ctx_, 0, sizeof(aio_ctx_));
                io_setup(128, &aio_ctx_);
                return fd_ != -1;
        }
        return false;
    }

    bool Append(const void* data, size_t size) {
        switch (method_) {
            case AppendMethod::WRITE_APPEND:
                return write(fd_, data, size) == static_cast<ssize_t>(size);

            case AppendMethod::WRITEV_APPEND: {
                struct iovec iov;
                iov.iov_base = const_cast<void*>(data);
                iov.iov_len = size;
                return writev(fd_, &iov, 1) == static_cast<ssize_t>(size);
            }

            case AppendMethod::FWRITE_APPEND:
                return fwrite(data, 1, size, file_) == size && fflush(file_) == 0;

            case AppendMethod::DIRECT_APPEND: {
                // For simplicity, assuming data fits in our aligned buffer
                if (size > aligned_buffer_size_) return false;
                memcpy(aligned_buffer_, data, size);
                return write(fd_, aligned_buffer_, size) == static_cast<ssize_t>(size);
            }

            case AppendMethod::AIO_APPEND: {
                struct iocb cb;
                struct iocb* cbs[1];
                io_event events[1];

                io_prep_pwrite(&cb, fd_, const_cast<void*>(data), size, 0);
                cbs[0] = &cb;

                if (io_submit(aio_ctx_, 1, cbs) != 1) return false;
                return io_getevents(aio_ctx_, 1, 1, events, NULL) == 1;
            }
        }
        return false;
    }

    void Close() {
        if (fd_ != -1) {
            close(fd_);
            fd_ = -1;
        }

        if (file_) {
            fclose(file_);
            file_ = nullptr;
        }

        if (aio_ctx_) {
            io_destroy(aio_ctx_);
            aio_ctx_ = 0;
        }

        if (aligned_buffer_) {
            free(aligned_buffer_);
            aligned_buffer_ = nullptr;
        }
    }

    std::string GetMethodName() const {
        switch (method_) {
            case AppendMethod::WRITE_APPEND:   return "O_APPEND with write()";
            case AppendMethod::WRITEV_APPEND:  return "writev() with O_APPEND";
            case AppendMethod::FWRITE_APPEND:  return "FILE streams (fwrite)";
            case AppendMethod::DIRECT_APPEND:  return "Direct I/O (O_DIRECT)";
            case AppendMethod::AIO_APPEND:     return "Linux AIO";
        }
        return "Unknown";
    }
};

void RunBenchmark(AppendMethod method, int num_processes, int ops_per_process, size_t data_size) {
    std::string filename = "/tmp/benchmark_" + std::to_string(static_cast<int>(method));

    // Delete file if it exists
    unlink(filename.c_str());

    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<pid_t> pids;
    for (int i = 0; i < num_processes; i++) {
        pid_t pid = fork();
        if (pid == 0) { // Child process
            FileAppender appender(method);
            if (!appender.Open(filename)) {
                exit(1);
            }

            std::vector<char> data(data_size, 'X');
            for (int j = 0; j < ops_per_process; j++) {
                appender.Append(data.data(), data.size());
            }

            exit(0);
        } else { // Parent process
            pids.push_back(pid);
        }
    }

    // Wait for all child processes
    for (pid_t pid : pids) {
        int status;
        waitpid(pid, &status, 0);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    FileAppender appender(method);
    std::cout << "Method: " << appender.GetMethodName() << std::endl;
    std::cout << "Time: " << duration.count() << " ms" << std::endl;

    // Get file size
    struct stat st;
    if (stat(filename.c_str(), &st) == 0) {
        std::cout << "File size: " << st.st_size << " bytes" << std::endl;
    }
}

int main() {
    // Run benchmarks for each method with different configurations
    const int NUM_PROCESSES = 4;
    const int OPS_PER_PROCESS = 1000;
    const size_t DATA_SIZE = 1024; // 1KB per write

    std::cout << "Running benchmarks with " << NUM_PROCESSES << " processes, "
              << OPS_PER_PROCESS << " operations per process, "
              << DATA_SIZE << " bytes per operation\n" << std::endl;

    RunBenchmark(AppendMethod::WRITE_APPEND, NUM_PROCESSES, OPS_PER_PROCESS, DATA_SIZE);
    std::cout << std::endl;

    RunBenchmark(AppendMethod::WRITEV_APPEND, NUM_PROCESSES, OPS_PER_PROCESS, DATA_SIZE);
    std::cout << std::endl;

    RunBenchmark(AppendMethod::FWRITE_APPEND, NUM_PROCESSES, OPS_PER_PROCESS, DATA_SIZE);
    std::cout << std::endl;

    RunBenchmark(AppendMethod::DIRECT_APPEND, NUM_PROCESSES, OPS_PER_PROCESS, DATA_SIZE);
    std::cout << std::endl;

    RunBenchmark(AppendMethod::AIO_APPEND, NUM_PROCESSES, OPS_PER_PROCESS, DATA_SIZE);

    return 0;
}


