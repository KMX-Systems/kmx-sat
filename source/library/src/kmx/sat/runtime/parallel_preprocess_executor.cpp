/// @file library/src/kmx/sat/runtime/parallel_preprocess_executor.cpp
/// @brief Out-of-line definitions declared by kmx/sat/runtime/parallel_preprocess_executor.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/runtime/parallel_preprocess_executor.hpp>

namespace kmx::sat::runtime
{
    void parallel_preprocess_executor::run_parallel_pass() noexcept
    {
        ++runs_;
        if (thread_budget_ < 1u)
            thread_budget_ = 1u;
        last_parallel_chunk_size_ =
            (configured_work_item_count_ == 0u) ?
                0u :
                configured_work_item_count_ / thread_budget_ + ((configured_work_item_count_ % thread_budget_ != 0u) ? 1u : 0u);
        total_processed_work_item_count_ += configured_work_item_count_;
    }

    void parallel_preprocess_executor::merge_deterministic_result() noexcept
    {
        if (merges_ < runs_)
        {
            ++merges_;
            return;
        }
        ++idle_merge_skip_count_;
    }

    void parallel_preprocess_executor::set_thread_budget(std::uint32_t thread_budget) noexcept
    {
        thread_budget_ = thread_budget;
        if (thread_budget_ < 1u)
            thread_budget_ = 1u;
        if (thread_budget_ > 256u)
            thread_budget_ = 256u;
    }

    [[nodiscard]] parallel_preprocess_executor::execution_metrics parallel_preprocess_executor::execution_metrics_snapshot() const noexcept
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

    void parallel_preprocess_executor::reset_execution_metrics() noexcept
    {
        runs_ = 0u;
        merges_ = 0u;
        thread_budget_ = 1u;
        idle_merge_skip_count_ = 0u;
        configured_work_item_count_ = 0u;
        last_parallel_chunk_size_ = 0u;
        total_processed_work_item_count_ = 0u;
    }

    bool parallel_preprocess_executor::execution_metrics_monotonic(const execution_metrics& before, const execution_metrics& after) noexcept
    {
        return (after.runs >= before.runs) && (after.merges >= before.merges) &&
               (after.idle_merge_skip_count >= before.idle_merge_skip_count) &&
               (after.total_processed_work_item_count >= before.total_processed_work_item_count);
    }
}
