#include "indexing/btree.hpp"
#include "log/logger.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fmt/core.h>
#include <iostream>
#include <sys/resource.h>
#include <vector>

namespace {
    struct BenchmarkResult {
        std::string name;
        uint64_t ops_total;
        double duration_ms;
        double throughput_ops_sec;
        double avg_latency_us;
        uint64_t peak_rss_bytes;
        uint64_t final_rss_bytes;

        auto get_throughput_str() const -> std::string {
            if (throughput_ops_sec >= 1e6) {
                return fmt::format("{:.2f}M", throughput_ops_sec / 1e6);
            } else if (throughput_ops_sec >= 1e3) {
                return fmt::format("{:.2f}K", throughput_ops_sec / 1e3);
            }
            return fmt::format("{:.0f}", throughput_ops_sec);
        }
    };

    auto get_memory_usage() -> uint64_t {
        struct rusage usage;
        if (getrusage(RUSAGE_SELF, &usage) == 0) {
#ifdef __APPLE__
            // macOS: ru_maxrss is already reported in bytes
            return static_cast<uint64_t>(usage.ru_maxrss);
#else
            // Linux: ru_maxrss is reported in KB, convert to bytes
            return static_cast<uint64_t>(usage.ru_maxrss) * 1024;
#endif
        }
        return 0;
    }

    auto format_bytes(uint64_t bytes) -> std::string {
        const double bytes_d = static_cast<double>(bytes);
        if (bytes >= 1024 * 1024) {
            return fmt::format("{:.2f} MB", bytes_d / (1024.0 * 1024.0));
        } else if (bytes >= 1024) {
            return fmt::format("{:.2f} KB", bytes_d / 1024.0);
        }
        return fmt::format("{} B", bytes);
    }

    template <typename Func>
    auto measure_operation(std::string name, uint64_t iterations, Func &&op) -> BenchmarkResult {
        // Cleanup
        std::remove("embrace.wal");
        std::remove("embrace.wal.snapshot");

        embrace::indexing::Btree tree("embrace.wal");
        tree.set_checkpoint_interval(50000); // Reduced checkpoints during ops

        const uint64_t peak_rss_before = get_memory_usage();
        const auto start = std::chrono::high_resolution_clock::now();

        op(tree, iterations);

        const auto end = std::chrono::high_resolution_clock::now();
        const uint64_t peak_rss_after = get_memory_usage();

        const double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        const double iterations_d = static_cast<double>(iterations);
        const double throughput = (iterations_d / duration_ms) * 1000.0; // ops/sec
        const double latency_us = (duration_ms * 1000.0) / iterations_d;
        const uint64_t rss_delta =
            peak_rss_after > peak_rss_before ? peak_rss_after - peak_rss_before : 0;

        return BenchmarkResult{.name = name,
                               .ops_total = iterations,
                               .duration_ms = duration_ms,
                               .throughput_ops_sec = throughput,
                               .avg_latency_us = latency_us,
                               .peak_rss_bytes = rss_delta,
                               .final_rss_bytes = peak_rss_after};
    }

    template <typename Setup, typename Func>
    auto measure_operation_with_setup(std::string name, uint64_t iterations, Setup &&setup,
                                      Func &&op) -> BenchmarkResult {
        std::remove("embrace.wal");
        std::remove("embrace.wal.snapshot");

        embrace::indexing::Btree tree("embrace.wal");
        tree.set_checkpoint_interval(50000);

        setup(tree, iterations);

        const uint64_t peak_rss_before = get_memory_usage();
        const auto start = std::chrono::high_resolution_clock::now();

        op(tree, iterations);

        const auto end = std::chrono::high_resolution_clock::now();
        const uint64_t peak_rss_after = get_memory_usage();

        const double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();
        const double iterations_d = static_cast<double>(iterations);
        const double throughput = (iterations_d / duration_ms) * 1000.0;
        const double latency_us = (duration_ms * 1000.0) / iterations_d;
        const uint64_t rss_delta =
            peak_rss_after > peak_rss_before ? peak_rss_after - peak_rss_before : 0;

        return BenchmarkResult{.name = name,
                               .ops_total = iterations,
                               .duration_ms = duration_ms,
                               .throughput_ops_sec = throughput,
                               .avg_latency_us = latency_us,
                               .peak_rss_bytes = rss_delta,
                               .final_rss_bytes = peak_rss_after};
    }

    auto benchmark_sequential_insert() -> BenchmarkResult {
        return measure_operation("Sequential Insert (FIFO order)", 100000,
                                 [](embrace::indexing::Btree &tree, uint64_t n) {
                                     for (uint64_t i = 0; i < n; i++) {
                                         std::string key = fmt::format("key_{:08d}", i);
                                         std::string val =
                                             fmt::format("value_data_{}_xxxxx_padding_xxxxxxxx", i);
                                         [[maybe_unused]] auto status = tree.put(key, val);
                                     }
                                 });
    }

    auto benchmark_random_insert() -> BenchmarkResult {
        // Pre-generate random sequence deterministically
        std::vector<uint64_t> shuffled(50000);
        for (size_t i = 0; i < shuffled.size(); i++)
            shuffled[i] = i;

        // Simple LCG shuffle (deterministic)
        uint64_t seed = 12345;
        for (size_t i = shuffled.size() - 1; i > 0; i--) {
            seed = (seed * 1103515245 + 12345) & 0x7fffffff;
            size_t j = seed % (i + 1);
            std::swap(shuffled[i], shuffled[j]);
        }

        return measure_operation("Random Insert (Shuffled order)", 50000,
                                 [&shuffled](embrace::indexing::Btree &tree, uint64_t n) {
                                     for (uint64_t i = 0; i < n; i++) {
                                         uint64_t idx = shuffled[i];
                                         std::string key = fmt::format("rnd_{:08d}", idx);
                                         std::string val =
                                             fmt::format("value_random_{}_xxxxx_padding", idx);
                                         [[maybe_unused]] auto status = tree.put(key, val);
                                     }
                                 });
    }

    auto benchmark_sequential_read() -> BenchmarkResult {
        return measure_operation_with_setup(
            "Sequential Read (preloaded)", 100000,
            [](embrace::indexing::Btree &tree, uint64_t n) {
                for (uint64_t i = 0; i < n; i++) {
                    [[maybe_unused]] auto status =
                        tree.put(fmt::format("seq_{:08d}", i), fmt::format("val_{}", i));
                }
            },
            [](embrace::indexing::Btree &tree, uint64_t n) {
                uint64_t hits = 0;
                for (uint64_t i = 0; i < n; i++) {
                    auto result = tree.get(fmt::format("seq_{:08d}", i));
                    if (result)
                        hits++;
                }

                if (hits != n) {
                    LOG_WARN("Read hits: {} != expected {}", hits, n);
                }
            });
    }

    auto benchmark_point_lookup() -> BenchmarkResult {
        return measure_operation_with_setup(
            "Point Lookup (Hot cache)", 200000,
            [](embrace::indexing::Btree &tree, uint64_t n) {
                const uint64_t dataset_size = n / 2;
                for (uint64_t i = 0; i < dataset_size; i++) {
                    [[maybe_unused]] auto status =
                        tree.put(fmt::format("hotkey_{:06d}", i % 1000),
                                 fmt::format("data_{}", i));
                }
            },
            [](embrace::indexing::Btree &tree, uint64_t n) {
                uint64_t hits = 0;
                for (uint64_t i = 0; i < n; i++) {
                    auto result = tree.get(fmt::format("hotkey_{:06d}", i % 1000));
                    if (result)
                        hits++;
                }
                if (hits != n) {
                    LOG_WARN("Lookup hits: {} != expected {}", hits, n);
                }
            });
    }

    auto benchmark_update() -> BenchmarkResult {
        return measure_operation_with_setup(
            "Update (In-place modification)", 50000,
            [](embrace::indexing::Btree &tree, uint64_t n) {
                for (uint64_t i = 0; i < n; i++) {
                    [[maybe_unused]] auto status =
                        tree.put(fmt::format("upd_{:06d}", i), "initial_value");
                }
            },
            [](embrace::indexing::Btree &tree, uint64_t n) {
                for (uint64_t i = 0; i < n; i++) {
                    [[maybe_unused]] auto status =
                        tree.update(fmt::format("upd_{:06d}", i),
                                    fmt::format("updated_value_{}", i));
                }
            });
    }

    auto benchmark_mixed_workload() -> BenchmarkResult {
        return measure_operation_with_setup(
            "Mixed Workload (70% read, 20% write, 10% update)", 100000,
            [](embrace::indexing::Btree &tree, uint64_t) {
                for (uint64_t i = 0; i < 20000; i++) {
                    [[maybe_unused]] auto status =
                        tree.put(fmt::format("mix_{:06d}", i), fmt::format("initial_{}", i));
                }
            },
            [](embrace::indexing::Btree &tree, uint64_t n) {
                uint64_t next_write_key = 20000;

                for (uint64_t i = 0; i < n; i++) {
                    uint64_t op = i % 100;

                    if (op < 70) {
                        uint64_t key_idx = (i / 3) % 20000;
                        (void)tree.get(fmt::format("mix_{:06d}", key_idx));
                    } else if (op < 90) {
                        [[maybe_unused]] auto status =
                            tree.put(fmt::format("mix_{:06d}", next_write_key),
                                     fmt::format("new_{}", next_write_key));
                        next_write_key = (next_write_key + 1);
                        if (next_write_key > 40000)
                            next_write_key = 20000;
                    } else {
                        uint64_t key_idx = i % 20000;
                        [[maybe_unused]] auto status =
                            tree.update(fmt::format("mix_{:06d}", key_idx),
                                        fmt::format("updated_{}", i));
                    }
                }
            });
    }

    auto benchmark_delete_workload() -> BenchmarkResult {
        constexpr uint64_t total_keys = 40000;
        const uint64_t delete_ops = total_keys / 2;
        return measure_operation_with_setup(
            "Delete Workload (50% delete)", delete_ops,
            [=](embrace::indexing::Btree &tree, uint64_t) {
                for (uint64_t i = 0; i < total_keys; i++) {
                    [[maybe_unused]] auto status = tree.put(fmt::format("del_{:06d}", i), "data");
                }
            },
            [=](embrace::indexing::Btree &tree, uint64_t) {
                for (uint64_t i = 0; i < total_keys; i += 2) {
                    [[maybe_unused]] auto status = tree.remove(fmt::format("del_{:06d}", i));
                }
            });
    }

    auto benchmark_range_iteration() -> BenchmarkResult {
        constexpr uint64_t preload = 10000;
        // This measures one full tree scan; throughput/latency reflect a single traversal, not per-key cost.
        return measure_operation_with_setup(
            "Range Iteration (Full tree scan)", 1,
            [=](embrace::indexing::Btree &tree, uint64_t) {
                for (uint64_t i = 0; i < preload; i++) {
                    [[maybe_unused]] auto status =
                        tree.put(fmt::format("iter_{:08d}", i),
                                 fmt::format("payload_{}_xxxx", i));
                }
            },
            [=](embrace::indexing::Btree &tree, uint64_t) {
                uint64_t count = 0;
                tree.iterate_all([&count](const embrace::core::Key &,
                                          const embrace::core::Value &) { count++; });
                if (count != preload) {
                    LOG_WARN("Range iteration visited {} keys, expected {}", count, preload);
                }
            });
    }

    auto benchmark_recovery_time() -> BenchmarkResult {
        const uint64_t n = 50000;
        // Recovery is a one-shot event; metrics represent total recovery time for the dataset.
        return measure_operation_with_setup(
            "Recovery from WAL", 1,
            [n](embrace::indexing::Btree &tree, uint64_t) {
                for (uint64_t i = 0; i < n; i++) {
                    [[maybe_unused]] auto status =
                        tree.put(fmt::format("rec_{:06d}", i),
                                 fmt::format("recovery_data_{}", i));
                }
                [[maybe_unused]] auto status = tree.flush_wal();
            },
            [n](embrace::indexing::Btree &, uint64_t) {
                auto new_tree = std::make_unique<embrace::indexing::Btree>("embrace.wal");
                auto status = new_tree->recover_from_wal();
                if (!status.ok()) {
                    LOG_WARN("Recovery reported failure: {}", status.to_string());
                }
                auto sample = new_tree->get(fmt::format("rec_{:06d}", n / 2));
                if (!sample) {
                    LOG_WARN("Recovery sample key missing");
                }
            });
    }

} // namespace

auto main() -> int {
    using namespace embrace;

    // Initialize logging
    log::LogConfig config;
    config.level = log::Level::Warn; // Quiet during benchmarks
    config.console_output = false;
    log::Logger::instance().init(config);

    std::cout << "\n╔════════════════════════════════════════════════════════════════════╗\n"
              << "║           EmbraceDB - Comprehensive Benchmark Suite                  ║\n"
              << "╚════════════════════════════════════════════════════════════════════╝\n\n";

    std::vector<BenchmarkResult> results;

    std::cout << "[1/9] Running: Sequential Insert...\n" << std::flush;
    results.push_back(benchmark_sequential_insert());

    std::cout << "[2/9] Running: Random Insert...\n" << std::flush;
    results.push_back(benchmark_random_insert());

    std::cout << "[3/9] Running: Sequential Read...\n" << std::flush;
    results.push_back(benchmark_sequential_read());

    std::cout << "[4/9] Running: Point Lookup (Hot)...\n" << std::flush;
    results.push_back(benchmark_point_lookup());

    std::cout << "[5/9] Running: Update Operations...\n" << std::flush;
    results.push_back(benchmark_update());

    std::cout << "[6/9] Running: Mixed Workload...\n" << std::flush;
    results.push_back(benchmark_mixed_workload());

    std::cout << "[7/9] Running: Delete Workload...\n" << std::flush;
    results.push_back(benchmark_delete_workload());
    std::cout << "[8/9] Running: Range Iteration...\n" << std::flush;
    results.push_back(benchmark_range_iteration());
    std::cout << "[9/9] Running: Recovery Time...\n" << std::flush;
    results.push_back(benchmark_recovery_time());

    // Print results table
    std::cout << "\n" << std::string(98, '.') << "\n";
    std::cout << fmt::format("│ {:<35} │ {:>10} │ {:>12} │ {:>12} │ {:>10} │\n", "Benchmark",
                             "Ops/sec", "Avg Latency", "Time (ms)", "RAM Delta");
    std::cout << std::string(98, '.') << "\n";

    for (const auto &result : results) {
        std::cout << fmt::format("│ {:<35} │ {:>10} │ {:>10.2f} μs │ {:>10.2f} │ {:>10} │\n",
                                 result.name.substr(0, 35), result.get_throughput_str(),
                                 result.avg_latency_us, result.duration_ms,
                                 format_bytes(result.peak_rss_bytes));
    }

    std::cout << std::string(98, '.') << "\n\n";

    // Summary statistics
    std::cout << "╔════════════════════════════════════════════════════════════╗\n"
              << "║                    SUMMARY STATISTICS                       ║\n"
              << "╚════════════════════════════════════════════════════════════╝\n\n";

    uint64_t total_ops = 0;
    double total_time = 0;
    uint64_t max_memory = 0;

    for (const auto &r : results) {
        total_ops += r.ops_total;
        total_time += r.duration_ms;
        max_memory = std::max(max_memory, r.final_rss_bytes);
    }

    std::cout << fmt::format("Total Operations:     {:>15}\n", total_ops);
    std::cout << fmt::format("Total Time:           {:>12.2f} ms\n", total_time);
    const double agg_throughput =
        (static_cast<double>(total_ops) / total_time) * 1000.0; // ops/sec
    std::cout << fmt::format("Aggregate Throughput: {:>15} ops/sec\n",
                             fmt::format("{:.2f}M", agg_throughput / 1e6));
    std::cout << fmt::format("Peak Memory Usage:    {:>15}\n", format_bytes(max_memory));
    std::cout << "\n";

    // Per-operation analysis
    std::cout << "╔════════════════════════════════════════════════════════════╗\n"
              << "║              DETAILED OPERATION BREAKDOWN                   ║\n"
              << "╚════════════════════════════════════════════════════════════╝\n\n";

    for (const auto &result : results) {
        std::cout << fmt::format("{}:\n"
                                 "  Operations:    {}\n"
                                 "  Throughput:    {:.2f}M ops/sec\n"
                                 "  Latency:       {:.3f} μs/op\n"
                                 "  Total Time:    {:.2f} ms\n"
                                 "  Memory Delta:  {}\n\n",
                                 result.name, result.ops_total, result.throughput_ops_sec / 1e6,
                                 result.avg_latency_us, result.duration_ms,
                                 format_bytes(result.peak_rss_bytes));
    }

    // Cleanup
    std::remove("embrace.wal");
    std::remove("embrace.wal.snapshot");

    log::Logger::instance().shutdown();
    return 0;
}
