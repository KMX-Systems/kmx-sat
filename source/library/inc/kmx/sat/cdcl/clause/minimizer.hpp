/// @file inc/kmx/sat/cdcl/clause/minimizer.hpp
/// @brief Learned-clause minimization and clause-quality recomputation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstdint>
    #include <unordered_map>
    #include <unordered_set>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/cdcl/clause/storage.hpp>

namespace kmx::sat::cdcl::clause
{
    /// @brief Learned-clause minimization and clause-quality recomputation.
    /// @details
    /// A freshly 1-UIP-derived clause from `conflict_analyzer` often contains literals that are themselves
    /// implied by other literals already in the clause; `minimize_learned_clause` removes such redundant literals
    /// through recursive/self-subsuming resolution against the implication graph (the MiniSat/Kissat-style
    /// minimization pass), and `shrink_clause` commits the result in place via `clause::storage::shrink_clause`.
    /// `recompute_glue` updates the clause's literal-blocks-distance metric after minimization changes its literal
    /// set, since glue is computed from the set of distinct decision levels touched by the clause; `promote_if_needed`
    /// moves the clause to a higher `clause::database` tier when the recomputed glue crosses a quality threshold.
    class minimizer final
    {
    public:
        /// @brief Non-owning callback returning the current decision level of a variable.
        using level_lookup_t = std::uint32_t (*)(const void* context, variable var) noexcept;

        /// @brief Non-owning callback returning the reason clause of a variable.
        using reason_lookup_t = std::span<const literal> (*)(const void* context, variable var) noexcept;

        /// @brief Constructs a minimizer with no clause-specific state.
        /// @throws None (noexcept).
        minimizer() noexcept = default;

        void attach_storage(storage& storage) noexcept { storage_ = &storage; }

        void attach_database(database& database) noexcept { database_ = &database; }

        /// @brief Removes literals from a learned clause that are implied by its other literals.
        /// @param ref Reference to the learned clause to minimize.
        /// @throws None (noexcept).
        void minimize_learned_clause(const ref_t ref) noexcept;

        /// @brief Minimizes a learned clause through recursive reason-closure checks.
        /// @param ref Clause to minimize.
        /// @param level_of Callback returning a variable's current decision level.
        /// @param reason_of Callback returning a variable's implication reason.
        /// @param context Opaque callback context passed to both callbacks.
        /// @throws None (noexcept).
        void minimize_learned_clause(const ref_t ref, const level_lookup_t level_of, const reason_lookup_t reason_of,
                                     const void* context) noexcept;

        /// @brief Commits a clause's reduced literal set in place after minimization.
        /// @param ref Reference to the clause to shrink.
        /// @throws None (noexcept).
        void shrink_clause(const ref_t ref) noexcept;

        /// @brief Recomputes the glue (literal blocks distance) metric for a clause after its literals changed.
        /// @param ref Reference to the clause to recompute.
        /// @throws None (noexcept).
        void recompute_glue(const ref_t ref) noexcept;

        /// @brief Recomputes exact LBD/glue as the number of distinct decision levels in the stored clause.
        /// @param ref Clause whose current literals should be measured.
        /// @param level_of Callback returning a literal variable's decision level.
        /// @param context Opaque callback context.
        /// @throws None (noexcept).
        void recompute_glue(const ref_t ref, const level_lookup_t level_of, const void* context) noexcept;

        /// @brief Promotes a clause to a higher-quality tier if its recomputed glue justifies it.
        /// @param ref Reference to the clause to evaluate.
        /// @throws None (noexcept).
        void promote_if_needed(const ref_t ref) noexcept;

        [[nodiscard]] std::uint32_t minimized_clause_count() const noexcept { return static_cast<std::uint32_t>(minimized_.size()); }

        [[nodiscard]] std::uint32_t shrunk_clause_count() const noexcept { return static_cast<std::uint32_t>(shrunk_.size()); }

        [[nodiscard]] std::uint32_t promoted_clause_count() const noexcept { return static_cast<std::uint32_t>(promoted_.size()); }

        /// @brief Returns whether a specific clause was physically shrunk by this minimizer.
        bool was_shrunk(const ref_t ref) const noexcept { return ref.valid() && (shrunk_.find(ref.offset()) != shrunk_.end()); }

        [[nodiscard]] std::uint32_t last_glue() const noexcept { return last_glue_; }

    private:
        std::vector<std::uint32_t> distinct_scratch_ {};
        storage* storage_ {};
        database* database_ {};
        /// @brief Opens a fresh minimization scan, invalidating every mark from the previous one.
        /// @details The three membership sets this pass needs -- the clause's own literals, variables proved
        /// removable, and variables currently on the recursion stack -- were `std::unordered_set`s rebuilt per
        /// learned clause. Stamped arrays give the same answers without hashing or allocating, which matters
        /// because this runs once per conflict.
        void begin_minimization_scan() noexcept;

        static void stamp_at(std::vector<std::uint32_t>& stamps, const std::size_t index, const std::uint32_t stamp) noexcept;

        static bool has_stamp(const std::vector<std::uint32_t>& stamps, const std::size_t index, const std::uint32_t stamp) noexcept
        {
            return (index < stamps.size()) && (stamps[index] == stamp);
        }

        void mark_exact_literal(const literal::raw_t raw) noexcept
        {
            stamp_at(exact_literal_stamps_, static_cast<std::size_t>(raw), minimization_stamp_);
        }

        [[nodiscard]] bool is_exact_literal(const literal::raw_t raw) const noexcept
        {
            return has_stamp(exact_literal_stamps_, static_cast<std::size_t>(raw), minimization_stamp_);
        }

        void mark_removable(const std::uint32_t variable_index) noexcept
        {
            stamp_at(removable_stamps_, static_cast<std::size_t>(variable_index), minimization_stamp_);
        }

        [[nodiscard]] bool is_known_removable(const std::uint32_t variable_index) const noexcept
        {
            return has_stamp(removable_stamps_, static_cast<std::size_t>(variable_index), minimization_stamp_);
        }

        /// @brief Marks a variable as being expanded, reporting false if it already was (a cycle in the reasons).
        [[nodiscard]] bool begin_visit(const std::uint32_t variable_index) noexcept;

        void end_visit(const std::uint32_t variable_index) noexcept
        {
            if (static_cast<std::size_t>(variable_index) < visiting_stamps_.size())
                visiting_stamps_[variable_index] = 0u;
        }

        std::vector<literal> reason_stack_ {};
        std::vector<literal> minimized_literals_scratch_ {};
        std::vector<std::uint32_t> exact_literal_stamps_ {};
        std::vector<std::uint32_t> removable_stamps_ {};
        std::vector<std::uint32_t> visiting_stamps_ {};
        std::uint32_t minimization_stamp_ {};
        std::unordered_map<ref_t::offset_t, std::uint32_t> glue_ {};
        std::unordered_map<ref_t::offset_t, std::uint32_t> target_sizes_ {};
        std::unordered_set<ref_t::offset_t> minimized_ {};
        std::unordered_set<ref_t::offset_t> shrunk_ {};
        std::unordered_set<ref_t::offset_t> promoted_ {};
        std::uint32_t last_glue_ {};
    };
}
