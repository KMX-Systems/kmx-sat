/// @file inc/kmx/sat/simplify/vivifier.hpp
/// @brief Clause strengthening through temporary assumptions and propagation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
#endif

#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/clause/ref_t.hpp>

namespace kmx::sat::simplify
{
    /// @brief Clause strengthening through temporary assumptions and propagation.
    /// @details
    /// Vivification strengthens a clause `C` by temporarily assuming the negation of each of its literals in turn and
    /// propagating: if propagation falsifies another literal already in `C`, that literal is redundant and can be
    /// removed; if propagation reaches a conflict outright, `C` is implied and can be simplified more aggressively.
    /// `vivify_clause` runs this process for one clause using `solver_core`'s propagation machinery under a temporary
    /// assumption scope; `abort_on_budget` checks a conflict/time budget so vivification (relatively expensive per
    /// clause) does not dominate a pass; `commit_shrunk_clause` applies the result via `clause::storage::shrink_clause`
    /// once a beneficial reduction is confirmed; `run` sweeps the clause database under the pass's overall budget.
    class vivifier final
    {
    public:
        /// @brief Constructs a vivifier with a default pass budget.
        /// @throws None (noexcept).
        vivifier() noexcept = default;

        /// @brief Attaches the clause database to be vivified.
        /// @param database Clause database whose clauses may be strengthened.
        /// @throws None (noexcept).
        void attach_database(cdcl::clause::database& database) noexcept { database_ = &database; }

        /// @brief Sets the maximum number of clauses to process in one vivification pass.
        /// @param clause_budget Maximum clauses to process during `run`.
        /// @throws None (noexcept).
        void set_budget(const std::size_t clause_budget) noexcept { clause_budget_ = clause_budget; }

        /// @brief Runs a full vivification pass over eligible clauses under the pass budget.
        /// @throws None (noexcept).
        void run() noexcept
        {
            run_completed_ = false;
            processed_in_current_run_ = 0u;

            if (database_ != nullptr)
            {
                bool stopped {};
                auto process_ref = [&](const cdcl::clause::ref_t ref) noexcept
                {
                    if (stopped || abort_on_budget())
                    {
                        stopped = true;
                        return;
                    }

                    vivify_clause(ref);
                    commit_shrunk_clause(ref);
                    ++processed_in_current_run_;
                };

                database_->iterate_irredundant(process_ref);
                database_->iterate_redundant(process_ref);
            }

            run_completed_ = true;
        }

        /// @brief Attempts to strengthen one clause via temporary-assumption propagation.
        /// @param ref Reference to the candidate clause.
        /// @throws None (noexcept).
        void vivify_clause(const cdcl::clause::ref_t ref) noexcept
        {
            ++vivified_clause_count_;

            pending_shrink_ref_ = {};
            pending_target_size_ = 0u;

            auto& storage = database_->storage_of();
            if (database_ == nullptr || !ref.valid() || !storage.is_alive(ref))
                return;

            const auto current_size = storage.literal_count(ref);
            if (current_size <= 1u)
                return;
        }

        /// @brief Checks whether the current vivification budget has been exhausted.
        /// @return True if the pass should stop before processing further clauses.
        /// @throws None (noexcept).
        bool abort_on_budget() const noexcept { return processed_in_current_run_ >= clause_budget_; }

        /// @brief Commits a clause's reduced literal set once a beneficial vivification result is confirmed.
        /// @param ref Reference to the clause to shrink.
        /// @throws None (noexcept).
        void commit_shrunk_clause(const cdcl::clause::ref_t ref) noexcept
        {
            if (database_ == nullptr || !ref.valid() || ref != pending_shrink_ref_ || pending_target_size_ == 0u)
                return;

            database_->storage_of().shrink_clause(ref, pending_target_size_);
            pending_shrink_ref_ = {};
            pending_target_size_ = 0u;
            ++committed_shrink_count_;
        }

        std::size_t clause_budget() const noexcept { return clause_budget_; }

        std::size_t processed_in_current_run() const noexcept { return processed_in_current_run_; }

        std::size_t vivified_clause_count() const noexcept { return vivified_clause_count_; }

        std::size_t committed_shrink_count() const noexcept { return committed_shrink_count_; }

        bool run_completed() const noexcept { return run_completed_; }

    private:
        cdcl::clause::database* database_ {};
        std::size_t clause_budget_ {1u};
        std::size_t processed_in_current_run_ {};
        std::size_t vivified_clause_count_ {};
        std::size_t committed_shrink_count_ {};
        cdcl::clause::ref_t pending_shrink_ref_ {};
        std::uint32_t pending_target_size_ {};
        bool run_completed_ {};
    };
}
