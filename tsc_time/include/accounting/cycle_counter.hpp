#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>

// The basic idea of this class is to use CPU hardware in order to provide an estimate for the number of cycles since some epoch (usually VM start).
// The typical convention would be to use vDSO-accelerated clock calls (e.g., `gettimeofday()`), but
// * the fact of vDSO acceleration is not universal, even on contemporary cloud providers
// * even when those interfaces are so accelerated, the clock hardware may be virtualized on the VM, degrading performance
// * we want a _total_ count of block time, not just CPU time
// * calls may be quite granular in order to properly account for consumption and block time
// * there are OS differences between windows/linux
//
// An important implementation detail.  This project supports both 32- and 64-bit modes.  Unfortunately, we build for a 32-bit target, which may
// preclude some of these operations, even though the physical CPU will absolutely support these features.
//
// This code directly calls CPU hardware (should generally be available even in virtualized environments) to provide a cycle count.
// In order to convert normalized cycles to time, a calibration procedure is required. This should only be necessary once,
// although it is possible for this to become inaccurate under load.
class CycleCounter {
private:
    // Calibration generally only needs to be done once, since these counters are normalized by contemporary hardware.
    inline static double cycles_per_ns{0.0};
    inline static std::atomic<bool> is_calibrated{false};

#if defined(__x86_64__)
    static inline uint64_t read_cycles() {
        uint32_t low, high;
        asm volatile("rdtsc" : "=a" (low), "=d" (high));
        return ((uint64_t)high << 32) | low;
    }

    static inline uint64_t read_cycles_precise() {
        uint32_t low, high;
        asm volatile("cpuid\n\t"
                     "rdtsc" : "=a" (low), "=d" (high) :: "%rbx", "%rcx");
        return ((uint64_t)high << 32) | low;
    }
#elif defined(__i386__) || defined(_M_IX86)
    // Does  MSCV understand __i386__ or do we really need _M_IX86?
    static inline uint64_t read_cycles() {
        uint32_t low, high;
        asm volatile("rdtsc" : "=a" (low), "=d" (high));
        return ((uint64_t)high << 32) | low;
    }

    static inline uint64_t read_cycles_precise() {
        uint32_t low, high;
        asm volatile("cpuid\n\t"
                     "rdtsc" : "=a" (low), "=d" (high) :: "%ebx", "%ecx");
        return ((uint64_t)high << 32) | low;
    }
#elif defined(__aarch64__)
    static inline uint64_t read_cycles() {
        uint64_t cycles;
        asm volatile("mrs %0, cntvct_el0" : "=r" (cycles));
        return cycles;
    }

    static inline uint64_t read_cycles_precise() {
        // Add memory barrier for more precise measurement
        asm volatile("isb" ::: "memory");
        uint64_t cycles;
        asm volatile("mrs %0, cntvct_el0" : "=r" (cycles));
        return cycles;
    }
#else
    #error "Architecture not supported"
#endif

    // Calibrate the counter by running for a fixed amount of wall time
    static void calibrate(uint64_t duration_us, uint64_t num_samples) {
        // The basic observations here are
        // * the hardware we're calling is frequency-normalized
        // * however, our code may be scheduled/descheduled or have more time in library calls etc
        // * conceptually, the fastest time is the right time
        // * but let's take a median to fold in other overheads to get better precision in the field
        using namespace std::chrono;
        std::vector<double> samples{};
        samples.reserve(num_samples);

        for (int i = 0; i < num_samples; ++i) {
            auto start_time = high_resolution_clock::now();
            auto start_cycles = read_cycles_precise();

            // Now loop until the desired duration has passed
            auto now = high_resolution_clock::now();
            while (duration_cast<microseconds>(now - start_time).count() < duration_us) {
                now = high_resolution_clock::now();
            }
            auto end_cycles = read_cycles_precise();

            // Calculate elapsed time and cycles
            double elapsed_ns = duration_cast<nanoseconds>(now - start_time).count();
            uint64_t elapsed_cycles = end_cycles - start_cycles;
            samples.push_back(static_cast<double>(elapsed_cycles + 0.0) / elapsed_ns);
        }

        std::sort(samples.begin(), samples.end());
        cycles_per_ns = samples[num_samples / 2];
        if (num_samples % 2 == 0) {
            cycles_per_ns = (samples[num_samples / 2] + samples[num_samples / 2 - 1]) / 2;
        }
        is_calibrated.store(true);
    }


public:
    static inline uint64_t get_cycles() {
        return read_cycles();
    }

    // Get a more precise cycle count (with serialization)
    static inline uint64_t get_cycles_precise() {
        return read_cycles_precise();
    }

    // Convert cycles to nanoseconds (if calibrated)
    static uint64_t cycles_to_ns(uint64_t cycles) {
        if (!is_calibrated) {
            return 0; // Indicate error condition
        }
        return static_cast<uint64_t>(static_cast<double>(cycles) / cycles_per_ns);
    }

    // Returns true if the counter has been calibrated; also ensures calibration only runs once
    static bool initialize() {
        static std::atomic<bool> initialized{false};

        bool expected = false;
        if (initialized.compare_exchange_strong(expected, true)) {
            calibrate(100, 25);
        }
        return is_calibrated.load();
    }
};
