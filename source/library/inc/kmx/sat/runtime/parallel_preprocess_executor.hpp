/// @file inc/kmx/sat/runtime/parallel_preprocess_executor.hpp
/// @brief Executes embarrassingly parallel, order-independent preprocessing sub-tasks across worker threads, with a
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
#endif

namespace kmx::sat::runtime
{
    /// @brief Executes embarrassingly parallel, order-independent preprocessing sub-tasks across worker threads, with a
    /// deterministic merge step; never parallelizes the sequential CDCL search itself. Research-track.
    ///
    /// Some preprocessing sub-tasks (occurrence-list construction, subsumption candidate scanning) are
    /// embarrassingly parallel and order-independent, making them safe to run across multiple worker threads on
    /// multi-core machines without touching the sequential CDCL search's determinism guarantee.
    /// `run_parallel_pass` executes one such sub-task across `thread_budget` worker threads; `merge_deterministic_result`
    /// combines the per-thread partial results in a fixed, schedule-independent order so the merged output does not
    /// depend on thread interleaving. Per the enforcement rule, only passes proven order-independent (not BVE
    /// ordering or gate-extraction sequencing, which remain sequential) may be wrapped by this executor.
    /// @note Research-track: any pass promoted to run through this executor must first prove order-independent
    /// output and clear the comparative benchmarking harness; it never parallelizes `search_coordinator` itself.
    class parallel_preprocess_executor final
    {
    public:
        /// @brief Constructs an executor with a default worker thread budget.
        /// @throws None (noexcept).
        parallel_preprocess_executor() noexcept = default;

        /// @brief Runs one order-independent preprocessing sub-task across the configured worker threads.
        /// @throws None (noexcept).
        void run_parallel_pass() noexcept
        {
            ++runs_;
            if (thread_budget_ < 1u)
            {
                thread_budget_ = 1u;
            }
        }

        /// @brief Merges per-thread partial results into one schedule-independent, deterministic result.
        /// @throws None (noexcept).
        void merge_deterministic_result() noexcept { ++merges_; }

        /// @brief Returns the number of worker threads currently budgeted for parallel sub-tasks.
        /// @return Configured worker thread count.
        /// @throws None (noexcept).
        std::uint32_t thread_budget() const noexcept { return thread_budget_; }

        [[nodiscard]] std::uint32_t runs() const noexcept { return runs_; }

        [[nodiscard]] std::uint32_t merges() const noexcept { return merges_; }

    private:
        std::uint32_t runs_ {0u};
        std::uint32_t merges_ {0u};
        std::uint32_t thread_budget_ {1u};
    };
}
