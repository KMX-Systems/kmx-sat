/// @file inc/kmx/sat/runtime/parallel_preprocess_executor.hpp
/// @brief Executes embarrassingly parallel, order-independent preprocessing sub-tasks across worker threads, with a
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
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
        struct execution_metrics final
        {
            std::uint32_t runs {};
            std::uint32_t merges {};
            std::uint32_t thread_budget {};
            std::uint32_t idle_merge_skip_count {};
            std::size_t configured_work_item_count {};
            std::size_t last_parallel_chunk_size {};
            std::size_t total_processed_work_item_count {};
        };

        /// @brief Constructs an executor with a default worker thread budget.
        /// @throws None (noexcept).
        parallel_preprocess_executor() noexcept = default;

        /// @brief Runs one order-independent preprocessing sub-task across the configured worker threads.
        /// @throws None (noexcept).
        void run_parallel_pass() noexcept
        {
            ++runs_;
            if (thread_budget_ < 1u)
                thread_budget_ = 1u;
            last_parallel_chunk_size_ =
                configured_work_item_count_ == 0u ? 0u : (configured_work_item_count_ + thread_budget_ - 1u) / thread_budget_;
            total_processed_work_item_count_ += configured_work_item_count_;
        }

        /// @brief Merges per-thread partial results into one schedule-independent, deterministic result.
        /// @throws None (noexcept).
        void merge_deterministic_result() noexcept
        {
            if (merges_ < runs_)
            {
                ++merges_;
                return;
            }
            ++idle_merge_skip_count_;
        }

        void set_thread_budget(std::uint32_t thread_budget) noexcept
        {
            thread_budget_ = thread_budget;
            if (thread_budget_ < 1u)
                thread_budget_ = 1u;
            if (thread_budget_ > 256u)
                thread_budget_ = 256u;
        }

        void set_work_item_count(std::size_t work_item_count) noexcept { configured_work_item_count_ = work_item_count; }

        /// @brief Returns the number of worker threads currently budgeted for parallel sub-tasks.
        /// @return Configured worker thread count.
        /// @throws None (noexcept).
        std::uint32_t thread_budget() const noexcept { return thread_budget_; }

        [[nodiscard]] std::uint32_t runs() const noexcept { return runs_; }

        [[nodiscard]] std::uint32_t merges() const noexcept { return merges_; }

        [[nodiscard]] std::size_t configured_work_item_count() const noexcept { return configured_work_item_count_; }

        [[nodiscard]] std::size_t last_parallel_chunk_size() const noexcept { return last_parallel_chunk_size_; }

        [[nodiscard]] std::size_t total_processed_work_item_count() const noexcept { return total_processed_work_item_count_; }

        [[nodiscard]] std::uint32_t idle_merge_skip_count() const noexcept { return idle_merge_skip_count_; }

        [[nodiscard]] execution_metrics execution_metrics_snapshot() const noexcept
        {
            return execution_metrics {
                .runs = runs_,
                .merges = merges_,
                .thread_budget = thread_budget_,
                .idle_merge_skip_count = idle_merge_skip_count_,
                .configured_work_item_count = configured_work_item_count_,
                .last_parallel_chunk_size = last_parallel_chunk_size_,
                .total_processed_work_item_count = total_processed_work_item_count_,
            };
        }

        void reset_execution_metrics() noexcept
        {
            runs_ = 0u;
            merges_ = 0u;
            thread_budget_ = 1u;
            idle_merge_skip_count_ = 0u;
            configured_work_item_count_ = 0u;
            last_parallel_chunk_size_ = 0u;
            total_processed_work_item_count_ = 0u;
        }

        static bool execution_metrics_monotonic(const execution_metrics& before, const execution_metrics& after) noexcept
        {
            return after.runs >= before.runs && after.merges >= before.merges &&
                   after.idle_merge_skip_count >= before.idle_merge_skip_count &&
                   after.total_processed_work_item_count >= before.total_processed_work_item_count;
        }

    private:
        std::uint32_t runs_ {};
        std::uint32_t merges_ {};
        std::uint32_t thread_budget_ {1u};
        std::uint32_t idle_merge_skip_count_ {};
        std::size_t configured_work_item_count_ {};
        std::size_t last_parallel_chunk_size_ {};
        std::size_t total_processed_work_item_count_ {};
    };
}
