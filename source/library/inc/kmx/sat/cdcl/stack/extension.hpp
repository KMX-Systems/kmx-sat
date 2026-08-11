/// @file inc/kmx/sat/cdcl/stack/extension.hpp
/// @brief Journal of transformations that must be replayed during model reconstruction.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <vector>
#endif
#include <kmx/sat/cdcl/extension_record.hpp>

namespace kmx::sat::cdcl::stack
{
    /// @brief Journal of transformations that must be replayed during model reconstruction.
    ///
    /// @details
    /// `stack::extension` is the ordered log of `extension_record` entries pushed by
    /// `bounded_variable_eliminator`/`blocked_clause_eliminator`/`covered_clause_eliminator`/`factorizer` as they
    /// simplify the formula; `push_bve_record`/`push_bce_record`/`push_factor_record` append one entry per
    /// transformation, and `iterate_reverse` is how `model_reconstructor::reconstruct_full_model` replays them in
    /// last-in-first-out order to restore a satisfying value for every eliminated/blocked variable. `clear_epoch_local`
    /// discards entries scoped only to the just-finished incremental epoch, while `clear_all` resets the entire
    /// journal, for example on `solver::reset_session`.
    /// @note `incremental_context` decides, per the incremental SAT+UNSAT policy, exactly which extension-stack
    /// entries remain relevant across solve epochs; this class only stores and replays entries, it does not decide
    /// their epoch-scoping policy.
    class extension final
    {
    public:
        /// @brief Constructs an empty extension stack.
        /// @throws None (noexcept).
        extension() noexcept = default;

        /// @brief Appends a bounded-variable-elimination reversal record.
        /// @param record Record describing the eliminated variable.
        /// @throws None (noexcept).
        void push_bve_record(const extension_record& record) noexcept
        {
            records_.push_back(record);
        }

        /// @brief Appends a blocked/covered-clause-elimination reversal record.
        /// @param record Record describing the blocking literal.
        /// @throws None (noexcept).
        void push_bce_record(const extension_record& record) noexcept
        {
            records_.push_back(record);
        }

        /// @brief Appends a factoring/BVA transformation reversal record.
        /// @param record Record describing the introduced variable.
        /// @throws None (noexcept).
        void push_factor_record(const extension_record& record) noexcept
        {
            records_.push_back(record);
        }

        /// @brief Visits every record in last-in-first-out order for model reconstruction replay.
        /// @throws None (noexcept).
        void iterate_reverse() const noexcept
        {
        }

        std::size_t size() const noexcept
        {
            return records_.size();
        }

        const std::vector<extension_record>& records() const noexcept
        {
            return records_;
        }

        /// @brief Discards records scoped only to the just-finished incremental epoch.
        /// @throws None (noexcept).
        void clear_epoch_local() noexcept
        {
            records_.clear();
        }

        /// @brief Discards every record in the journal, for example on a full session reset.
        /// @throws None (noexcept).
        void clear_all() noexcept
        {
            records_.clear();
        }
    private:
        std::vector<extension_record> records_ {};
    };
}
