/// @file inc/kmx/sat/cdcl/stack/extension.hpp
/// @brief Journal of transformations that must be replayed during model reconstruction.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <cstdint>
    #include <span>
    #include <vector>
#endif
#include <kmx/sat/cdcl/extension_record.hpp>

namespace kmx::sat::cdcl::stack
{
    /// @brief Journal of transformations that must be replayed during model reconstruction.
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
        void push_bve_record(const extension_record& record) noexcept { records_.push_back(record); }

        /// @brief Appends a blocked/covered-clause-elimination reversal record.
        /// @param record Record describing the blocking literal.
        /// @throws None (noexcept).
        void push_bce_record(const extension_record& record) noexcept { records_.push_back(record); }

        /// @brief Appends a factoring/BVA transformation reversal record.
        /// @param record Record describing the introduced variable.
        /// @throws None (noexcept).
        void push_factor_record(const extension_record& record) noexcept { records_.push_back(record); }

        /// @brief Returns the current end of the witness buffer, to be used as a record's `witness_begin`.
        /// @return Offset one past the last stored witness literal.
        /// @throws None (noexcept).
        [[nodiscard]] std::uint32_t witness_mark() const noexcept { return static_cast<std::uint32_t>(witness_literals_.size()); }

        /// @brief Appends one clause to the witness buffer, terminated by a zero literal.
        /// @details Witness clauses are the clauses a transformation removed. They are stored flat, in one buffer,
        /// so that an extension record stays a small value type instead of owning a nested container per entry.
        /// @param literals Clause literals to store.
        /// @throws None (noexcept).
        void append_witness_clause(const std::span<const literal> literals) noexcept
        {
            witness_literals_.insert(witness_literals_.end(), literals.begin(), literals.end());
            witness_literals_.push_back(literal {});
        }

        /// @brief Appends a bounded-variable-elimination record covering the witness clauses stored since `mark`.
        /// @param eliminated_variable Variable removed by resolution.
        /// @param mark Value returned by `witness_mark` before the clauses were appended.
        /// @throws None (noexcept).
        void push_bve_elimination(const variable eliminated_variable, const std::uint32_t mark) noexcept
        {
            records_.push_back(extension_record {bve_elimination {eliminated_variable, mark, witness_mark()}});
        }

        /// @brief Appends a blocked-clause-elimination record covering the witness clause stored since `mark`.
        /// @param blocking_literal Literal on which the removed clause was blocked.
        /// @param mark Value returned by `witness_mark` before the clause was appended.
        /// @throws None (noexcept).
        void push_bce_blocking(const literal blocking_literal, const std::uint32_t mark) noexcept
        {
            records_.push_back(extension_record {bce_blocking {blocking_literal, mark, witness_mark()}});
        }

        /// @brief Appends a factoring record for `introduced_variable`, whose defining literals were appended as one
        /// witness clause since `mark`.
        void push_factor_transformation(const variable introduced_variable, const std::uint32_t mark) noexcept
        {
            records_.push_back(extension_record {factor_transformation {introduced_variable, mark, witness_mark()}});
        }

        /// @brief Returns the flat witness buffer the records index into.
        /// @return Read-only view of every stored witness literal.
        /// @throws None (noexcept).
        [[nodiscard]] std::span<const literal> witness_literals() const noexcept { return witness_literals_; }

        std::size_t size() const noexcept { return records_.size(); }

        const std::vector<extension_record>& records() const noexcept { return records_; }

        /// @brief Discards records scoped only to the just-finished incremental epoch.
        /// @throws None (noexcept).
        void clear_epoch_local() noexcept
        {
            records_.clear();
            witness_literals_.clear();
        }

        /// @brief Discards every record in the journal, for example on a full session reset.
        /// @throws None (noexcept).
        void clear_all() noexcept
        {
            records_.clear();
            witness_literals_.clear();
        }

    private:
        std::vector<extension_record> records_ {};
        std::vector<literal> witness_literals_ {};
    };
}
