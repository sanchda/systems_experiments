#include "libmmlog.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <libaio.h>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
enum class AppendMethod {
    WRITE_APPEND,   // O_APPEND with write()
    WRITEV_APPEND,  // writev() with O_APPEND
    FWRITE_APPEND,  // FILE streams with "a" mode
    DIRECT_APPEND,  // O_DIRECT + O_APPEND
    AIO_APPEND,     // Linux AIO with O_APPEND
    MMLOG_APPEND,   // Memory-mapped log
};

class FileAppender {
  private:
    AppendMethod method_;
    int fd_ = -1;
    FILE* file_ = nullptr;
    io_context_t aio_ctx_ = 0;
    void* aligned_buffer_ = nullptr;
    size_t aligned_buffer_size_ = 0;
    log_handle_t* mmlog_handle_ = nullptr;

  public:
    FileAppender(AppendMethod method) : method_(method)
    {
    }

    ~FileAppender()
    {
        Close();
    }

    bool Open(const std::string& filename)
    {
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
                    aligned_buffer_size_ = 4096;  // Typical page size
                    auto _ = posix_memalign(&aligned_buffer_, aligned_buffer_size_, aligned_buffer_size_);
                    (void)_;
                }
                return fd_ != -1 && aligned_buffer_ != nullptr;

            case AppendMethod::AIO_APPEND:
                fd_ = open(filename.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
                memset(&aio_ctx_, 0, sizeof(aio_ctx_));
                io_setup(128, &aio_ctx_);
                return fd_ != -1;
            case AppendMethod::MMLOG_APPEND:
                mmlog_handle_ = mmlog_open(filename.c_str(), 8 * 4096, 4);
                bool ret = mmlog_handle_ != nullptr;
                if (!ret) {
                    std::cout << "Could not open mmlog: " << mmlog_strerror_cur() << std::endl;
                }
                return ret;
        }
        return false;
    }

    bool Append(const void* data, size_t size)
    {
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
                if (size > aligned_buffer_size_)
                    return false;
                memcpy(aligned_buffer_, data, size);
                return write(fd_, aligned_buffer_, size) == static_cast<ssize_t>(size);
            }

            case AppendMethod::AIO_APPEND: {
                struct iocb cb;
                struct iocb* cbs[1];
                io_event events[1];

                io_prep_pwrite(&cb, fd_, const_cast<void*>(data), size, 0);
                cbs[0] = &cb;

                if (io_submit(aio_ctx_, 1, cbs) != 1)
                    return false;
                return io_getevents(aio_ctx_, 1, 1, events, NULL) == 1;
            }
            case AppendMethod::MMLOG_APPEND:
                return mmlog_insert(mmlog_handle_, data, size);
        }
        return false;
    }

    void Close()
    {
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

        if (mmlog_handle_) {
            mmlog_trim(mmlog_handle_);
            mmlog_handle_ = nullptr;
        }
    }

    std::string GetMethodName() const
    {
        switch (method_) {
            case AppendMethod::WRITE_APPEND:
                return "O_APPEND with write()";
            case AppendMethod::WRITEV_APPEND:
                return "writev() with O_APPEND";
            case AppendMethod::FWRITE_APPEND:
                return "FILE streams (fwrite)";
            case AppendMethod::DIRECT_APPEND:
                return "Direct I/O (O_DIRECT)";
            case AppendMethod::AIO_APPEND:
                return "Linux AIO";
            case AppendMethod::MMLOG_APPEND:
                return "mmlog";
        }
        return "Unknown";
    }
};

// Structure to hold benchmark results
struct BenchmarkResult {
    std::string method_name;
    long long duration_ms;
    double time_per_call_us;
    double throughput_gbps;
};

BenchmarkResult RunBenchmark(AppendMethod method, int num_processes, int ops_per_process, size_t data_size)
{
    std::string filename = "/tmp/benchmark_" + std::to_string(static_cast<int>(method));

    // Delete file if it exists
    unlink(filename.c_str());

    // If it's an mmlog also delete filename.mmlog
    auto mmlog_name = filename + ".mmlog";
    unlink(mmlog_name.c_str());  // ignore error

    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<pid_t> pids;
    std::vector<char> data(data_size, 'X');
    for (int i = 0; i < num_processes; i++) {
        pid_t pid = fork();
        if (pid == 0) {  // Child process
            FileAppender appender(method);
            if (!appender.Open(filename)) {
                exit(1);
            }

            for (int j = 0; j < ops_per_process; j++) {
                appender.Append(data.data(), data.size());
            }

            exit(0);
        } else {  // Parent process
            pids.push_back(pid);
        }
    }

    // Wait for all child processes
    for (pid_t pid : pids) {
        int status;
        waitpid(pid, &status, 0);

        // Make sure the child process exited successfully
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            std::cerr << "Child process " << pid << " failed" << std::endl;
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    // Calculate metrics
    FileAppender appender(method);
    BenchmarkResult result;
    result.method_name = appender.GetMethodName();
    result.duration_ms = duration.count();
    int main(int argc, char* argv[])
    {
        // Run benchmarks for each method with different configurations
        const int NUM_PROCESSES = 4;
        const int OPS_PER_PROCESS = 100000;
        const size_t DATA_SIZE = 128;

        // Process command-line arguments
        bool run_all = argc == 1;  // Run all if no args provided
        std::vector<std::string> benchmarks_to_run;

        // Check for help flag
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            if (arg == "-h" || arg == "--help") {
                std::cout << "Usage: " << argv[0] << " [benchmark_names...]\n"
                          << "Available benchmarks:\n"
                          << "  mmlog   - Memory-mapped log\n"
                          << "  write   - O_APPEND with write()\n"
                          << "  writev  - writev() with O_APPEND\n"
                          << "  fwrite  - FILE streams (fwrite)\n"
                          << "  direct  - Direct I/O (O_DIRECT)\n"
                          << "  aio     - Linux AIO\n"
                          << "  all     - Run all benchmarks\n"
                          << "If no arguments are provided, all benchmarks will be run.\n";
                return 0;
            } else if (arg == "all") {
                run_all = true;
            } else {
                benchmarks_to_run.push_back(arg);
            }
        }

        std::cout << "Running benchmarks with " << NUM_PROCESSES << " processes, " << OPS_PER_PROCESS
                  << " operations per process, " << DATA_SIZE << " bytes per operation\n"
                  << std::endl;

        // Print markdown table header
        std::cout << "| Category | Total Time (ms) | Time per Call (μs) | Throughput (GB/s) |" << std::endl;
        std::cout << "|----------|----------------|-------------------|------------------|" << std::endl;

        std::vector<BenchmarkResult> results;

        // Run selected benchmarks
        if (run_all ||
            std::find(benchmarks_to_run.begin(), benchmarks_to_run.end(), "mmlog") != benchmarks_to_run.end()) {
            results.push_back(RunBenchmark(AppendMethod::MMLOG_APPEND, NUM_PROCESSES, OPS_PER_PROCESS, DATA_SIZE));
        }

        if (run_all ||
            std::find(benchmarks_to_run.begin(), benchmarks_to_run.end(), "write") != benchmarks_to_run.end()) {
            results.push_back(RunBenchmark(AppendMethod::WRITE_APPEND, NUM_PROCESSES, OPS_PER_PROCESS, DATA_SIZE));
        }

        if (run_all ||
            std::find(benchmarks_to_run.begin(), benchmarks_to_run.end(), "writev") != benchmarks_to_run.end()) {
            results.push_back(RunBenchmark(AppendMethod::WRITEV_APPEND, NUM_PROCESSES, OPS_PER_PROCESS, DATA_SIZE));
        }

        if (run_all ||
            std::find(benchmarks_to_run.begin(), benchmarks_to_run.end(), "fwrite") != benchmarks_to_run.end()) {
            results.push_back(RunBenchmark(AppendMethod::FWRITE_APPEND, NUM_PROCESSES, OPS_PER_PROCESS, DATA_SIZE));
        }

        if (run_all ||
            std::find(benchmarks_to_run.begin(), benchmarks_to_run.end(), "direct") != benchmarks_to_run.end()) {
            results.push_back(RunBenchmark(AppendMethod::DIRECT_APPEND, NUM_PROCESSES, OPS_PER_PROCESS, DATA_SIZE));
        }

        if (run_all ||
            std::find(benchmarks_to_run.begin(), benchmarks_to_run.end(), "aio") != benchmarks_to_run.end()) {
            results.push_back(RunBenchmark(AppendMethod::AIO_APPEND, NUM_PROCESSES, OPS_PER_PROCESS, DATA_SIZE));
        }

        // Output all results in markdown table format
        for (const auto& result : results) {
            std::cout << "| " << result.method_name << " | " << result.duration_ms << " | " << std::fixed
                      << std::setprecision(2) << result.time_per_call_us << " | " << std::fixed << std::setprecision(3)
                      << result.throughput_gbps << " |" << std::endl;
        }

        return 0;
    }
    std::cout << std::endl;
}

if (run_all || std::find(benchmarks_to_run.begin(), benchmarks_to_run.end(), "direct") != benchmarks_to_run.end()) {
    RunBenchmark(AppendMethod::DIRECT_APPEND, NUM_PROCESSES, OPS_PER_PROCESS, DATA_SIZE);
    std::cout << std::endl;
}

if (run_all || std::find(benchmarks_to_run.begin(), benchmarks_to_run.end(), "aio") != benchmarks_to_run.end()) {
    RunBenchmark(AppendMethod::AIO_APPEND, NUM_PROCESSES, OPS_PER_PROCESS, DATA_SIZE);
    std::cout << std::endl;
}

return 0;
}
