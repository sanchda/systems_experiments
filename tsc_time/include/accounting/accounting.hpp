#pragma once

#include "accounting/cycle_counter.hpp"
#include <vector>
#include <deque>
#include <atomic>
#include <mutex>
#include <cmath>
#include <chrono>

/**
 * PerformanceAccounting - A system for tracking performance metrics using
 * exponentially coarsened bucketing and providing feedback via PID control.
 *
 * This class is designed for high-frequency interactions (>1KHz) with minimal overhead.
 */
class PerformanceAccounting {
public:
    // Configuration struct to initialize the performance accounting system
    struct Config {
        // PID controller parameters
        double kp = 0.5;    // Proportional gain
        double ki = 0.05;   // Integral gain
        double kd = 0.1;    // Derivative gain

        // Time bucketing parameters
        size_t num_buckets = 16;                          // Number of buckets in time series
        double coarsening_factor = 2.0;                   // Factor by which buckets get coarser
        std::chrono::milliseconds base_resolution;        // Resolution of finest bucket
        std::chrono::milliseconds max_history;            // Maximum history

        // Target performance metric
        double target_latency_ms = 1.0;                   // Target operation latency in ms

        // Default constructor with reasonable defaults
        Config() {
            base_resolution = std::chrono::milliseconds(10);
            max_history = std::chrono::hours(1);
        }
    };

    // Dampening recommendation from PID controller
    struct DampeningRecommendation {
        double magnitude = 0.0;                   // Recommended dampening magnitude [0.0, 1.0]
        uint64_t timestamp = 0;                   // When this recommendation was generated
        bool requires_confirmation = false;       // Whether confirmation is required
    };

    // Constructor with configuration
    explicit PerformanceAccounting(const Config& config = Config{})
        : config(config), cycle_counter()
    {
        // Initialize buckets with exponentially increasing sizes
        initialize_buckets();

        // If the cycle counter has not been calibrated, do it now
        // (this is a thread-safe, idempotent operation)
        CycleCounter::initialize();
    }

    // Record the start of an operation
    inline void start_operation() {
        // This needs to be extremely cheap, just record the cycle count
        op_start = cycle_counter.get_cycles();
     }

    // Record the end of an operation and update metrics
    inline void end_operation() {
        // Calculate duration and convert to milliseconds
        uint64_t end = cycle_counter.get_cycles();
        uint64_t duration = end - op_start;
        double duration_ms = cycle_counter.cycles_to_ns(duration);

        // Fast path: just update atomic counters for most operations
        current_latency.store(duration_ms, std::memory_order_relaxed);
        operation_count.fetch_add(1, std::memory_order_relaxed);

        // Update buckets periodically (not every operation to minimize overhead)
        if (operation_count.load(std::memory_order_relaxed) % 100 == 0) {
            update_buckets(end, duration_ms);
        }
    }

    // Get the current dampening recommendation from the PID controller
    DampeningRecommendation getDampeningRecommendation() {
        // Run PID controller on our metrics to determine dampening
        std::lock_guard<std::mutex> lock(mutex);
        return computePIDOutput();
    }

    // Confirm that a dampening recommendation has been applied
    void confirmDampening(double applied_magnitude) {
        std::lock_guard<std::mutex> lock(mutex);
        last_applied_dampening = applied_magnitude;
        last_dampening_timestamp = cycle_counter.get_cycles();
    }

    // Get current performance metrics
    struct Metrics {
        double current_latency_ms;
        double avg_latency_ms_short_term;  // Recent window
        double avg_latency_ms_long_term;   // Longer window
        uint64_t operations_per_second;
        double dampening_magnitude;
    };

    Metrics getMetrics() const {
        std::lock_guard<std::mutex> lock(mutex);

        Metrics metrics;
        metrics.current_latency_ms = current_latency.load(std::memory_order_relaxed);

        // Calculate short and long term averages from buckets
        metrics.avg_latency_ms_short_term = calculateAverageLatency(0, 4);
        metrics.avg_latency_ms_long_term = calculateAverageLatency(0, buckets.size());

        // Calculate operations per second
        metrics.operations_per_second = operation_count.load(std::memory_order_relaxed) /
                                        (cycle_counter.cycles_to_ns(cycle_counter.get_cycles() - start_time) + 0.001);

        metrics.dampening_magnitude = last_applied_dampening;
        return metrics;
    }

private:
    // Structure for time buckets with exponentially increasing resolution
    struct TimeBucket {
        double sum_latency_ms = 0.0;
        uint64_t count = 0;
        uint64_t last_update_time = 0;
        std::chrono::milliseconds time_span{0};
    };

    // Initialize the bucket system
    void initialize_buckets() {
        buckets.resize(config.num_buckets);

        // First bucket has base resolution
        buckets[0].time_span = config.base_resolution;

        // Each subsequent bucket is coarser by the specified factor
        for (size_t i = 1; i < config.num_buckets; i++) {
            std::chrono::milliseconds span = std::chrono::milliseconds(
                static_cast<long long>(
                    config.base_resolution.count() * std::pow(config.coarsening_factor, i)
                )
            );
            buckets[i].time_span = span;
        }

        // Record the start time
        start_time = cycle_counter.get_cycles();
    }

    // Update buckets with new performance data
    void update_buckets(uint64_t current_time, double duration_ms) {
        std::lock_guard<std::mutex> lock(mutex);

        // Add to most recent bucket
        buckets[0].sum_latency_ms += duration_ms;
        buckets[0].count++;
        buckets[0].last_update_time = current_time;

        // Check if we need to shift data to coarser buckets
        for (size_t i = 0; i < buckets.size() - 1; i++) {
            // Calculate time since last update in ms
            double elapsed_ms = cycle_counter.cycles_to_ns(current_time - buckets[i+1].last_update_time);

            // If elapsed time exceeds this bucket's time span, shift data to the next bucket
            if (elapsed_ms > buckets[i].time_span.count()) {
                // Calculate average for this bucket
                double avg_latency = (buckets[i].count > 0) ?
                    buckets[i].sum_latency_ms / buckets[i].count : 0.0;

                // Add to next bucket
                buckets[i+1].sum_latency_ms += avg_latency;
                buckets[i+1].count++;
                buckets[i+1].last_update_time = current_time;

                // Reset this bucket
                buckets[i].sum_latency_ms = 0.0;
                buckets[i].count = 0;
            }
        }

        // For the last bucket, we discard data older than max history
        if (buckets.back().count > 0) {
            double elapsed_ms = cycle_counter.cycles_to_ns(current_time - buckets.back().last_update_time);
            if (elapsed_ms > config.max_history.count()) {
                buckets.back().sum_latency_ms = 0.0;
                buckets.back().count = 0;
            }
        }
    }

    // Calculate average latency across specified buckets
    double calculateAverageLatency(size_t start_idx, size_t end_idx) const {
        double sum_latency = 0.0;
        uint64_t total_count = 0;

        end_idx = std::min(end_idx, buckets.size());

        for (size_t i = start_idx; i < end_idx; i++) {
            sum_latency += buckets[i].sum_latency_ms;
            total_count += buckets[i].count;
        }

        return (total_count > 0) ? sum_latency / total_count : 0.0;
    }

    // Compute PID controller output based on current metrics
    DampeningRecommendation computePIDOutput() {
        uint64_t current_time = cycle_counter.get_cycles();

        // Calculate error (difference from target latency)
        double current_latency = current_latency.load(std::memory_order_relaxed);
        double error = current_latency - config.target_latency_ms;

        // Calculate time delta
        double dt_sec = cycle_counter.ticksToSec(current_time - last_pid_update);
        dt_sec = std::max(dt_sec, 0.001);  // Ensure minimum time step

        // Proportional term
        double p_term = config.kp * error;

        // Integral term (with anti-windup)
        error_integral += error * dt_sec;
        error_integral = std::clamp(error_integral, -100.0, 100.0);  // Prevent excessive accumulation
        double i_term = config.ki * error_integral;

        // Derivative term
        double error_derivative = (error - last_error) / dt_sec;
        double d_term = config.kd * error_derivative;

        // Calculate PID output
        double pid_output = p_term + i_term + d_term;

        // Clamp output to valid range [0.0, 1.0]
        double dampening_magnitude = std::clamp(pid_output, 0.0, 1.0);

        // Update state for next iteration
        last_error = error;
        last_pid_update = current_time;

        // Prepare recommendation
        DampeningRecommendation rec;
        rec.magnitude = dampening_magnitude;
        rec.timestamp = current_time;

        // Require confirmation if the change is significant
        if (std::abs(dampening_magnitude - last_applied_dampening) > 0.1) {
            rec.requires_confirmation = true;
        }

        return rec;
    }

private:
    Config config;
    CycleCounter cycle_counter;

    // Fast-path operation tracking
    std::atomic<double> current_latency{0.0};
    std::atomic<uint64_t> operation_count{0};
    uint64_t op_start{0};
    uint64_t start_time{0};

    // Bucketing system for historical data
    mutable std::mutex mutex;
    std::vector<TimeBucket> buckets;

    // PID controller state
    double last_error{0.0};
    double error_integral{0.0};
    uint64_t last_pid_update{0};

    // Dampening state
    double last_applied_dampening{0.0};
    uint64_t last_dampening_timestamp{0};
};

/**
 * Example usage for PerformanceAccounting:
 *
 * PerformanceAccounting::Config config;
 * config.target_latency_ms = 2.0;  // Target operation latency of 2ms
 *
 * PerformanceAccounting perf(config);
 *
 * // In your performance-critical loop:
 * perf.start_operation();
 * // ... perform operation ...
 * perf.end_operation();
 *
 * // Periodically check for dampening recommendations:
 * auto recommendation = perf.getDampeningRecommendation();
 * if (recommendation.requires_confirmation) {
 *     // Apply dampening based on recommendation.magnitude
 *     // ...
 *     // Confirm the applied dampening
 *     perf.confirmDampening(applied_magnitude);
 * }
 */
