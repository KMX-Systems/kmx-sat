/// @file inc/kmx/sat/proof/checker/lrat.hpp
/// @brief Stricter LRAT validation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <span>
    #include <unordered_map>
    #include <vector>
#endif
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof/clause/id.hpp>

namespace kmx::sat::proof::checker
{
    /// @brief Literals of every clause the checker still tracks, keyed by proof clause id.
    using clause_literal_map_t = std::unordered_map<clause::id::value_t, std::vector<literal>>;

    /// @brief Antecedent clause ids justifying each derived clause.
    using antecedent_map_t = std::unordered_map<clause::id::value_t, std::vector<clause::id::value_t>>;

    /// @brief Stricter LRAT validation.
    /// @details
    /// Where `checker::online` mirrors events for a cheap real-time sanity check, `checker::lrat` performs the
    /// stronger, format-specific validation LRAT's explicit antecedent chains enable: `check_chain` verifies that
    /// each derived clause's recorded antecedent sequence actually resolves to that clause (in linear time, since
    /// LRAT's antecedents remove the need to search for a RAT witness) by reverse unit propagation - every literal of
    /// the derived clause is assumed false, and the antecedents are replayed in order, each expected to become unit
    /// or falsified under the growing assignment until a conflict is reached; `validate_clause` performs the same
    /// check for one recorded derivation in isolation, useful for incremental/partial replay; `finalize_unsat`
    /// confirms some recorded clause is both empty and backed by a valid antecedent chain, certifying the UNSAT
    /// verdict end to end.
    /// @note This checker requires clause ids to remain stable exactly as `proof::clause::id_allocator` guarantees
    /// while `tracer::lrat` is active; it is the internal counterpart to running an external LRAT checker over the
    /// same proof, used for the proof replay testing requirement. Callers feed it clause content and antecedent
    /// chains via `record_clause`/`record_antecedents` as `proof_event`s arrive; `forget_clause` mirrors deletions.
    class lrat final
    {
    public:
        /// @brief Constructs an LRAT checker with no accumulated verification state.
        /// @throws None (noexcept).
        lrat() noexcept = default;

        /// @brief Records (or replaces) the literal snapshot for a clause identified by its stable proof id.
        /// @param clause_id Stable proof identity of the clause.
        /// @param literals Literal snapshot to associate with `clause_id`.
        /// @throws None (noexcept).
        void record_clause(const clause::id clause_id, const std::span<const literal> literals) noexcept;

        /// @brief Records the ordered LRAT antecedent chain justifying a derived clause.
        /// @param clause_id Stable proof identity of the derived clause.
        /// @param antecedents Ordered antecedent clause ids to replay during chain verification.
        /// @throws None (noexcept).
        void record_antecedents(const clause::id clause_id, const std::span<const clause::id> antecedents) noexcept;

        /// @brief Forgets a clause's recorded literals and antecedent chain once it is permanently deleted.
        /// @param clause_id Stable proof identity of the deleted clause.
        /// @throws None (noexcept).
        void forget_clause(const clause::id clause_id) noexcept;

        /// @brief Checks whether any antecedent chain has been recorded yet.
        /// @return True if at least one derived clause has a recorded antecedent chain.
        /// @throws None (noexcept).
        [[nodiscard]] bool has_recorded() const noexcept { return !antecedents_.empty(); }

        /// @brief Verifies that every recorded derived clause's antecedent chain actually resolves to that clause.
        /// @return True if every recorded antecedent chain is valid; false if none are recorded or any fails.
        /// @throws None (noexcept).
        [[nodiscard]] bool check_chain() const noexcept;

        /// @brief Validates one recorded clause's addition and antecedents in isolation.
        /// @param clause_id Stable proof identity of the clause to validate.
        /// @return True if the clause's recorded antecedent chain resolves to it.
        /// @throws None (noexcept).
        [[nodiscard]] bool validate_clause(const clause::id clause_id) const noexcept;

        /// @brief Confirms the proof concludes with the empty clause, certifying the UNSAT verdict.
        /// @return True if some recorded clause is empty and backed by a valid antecedent chain.
        /// @throws None (noexcept).
        [[nodiscard]] bool finalize_unsat() const noexcept;

    private:
        /// @brief Performs reverse unit propagation: assumes `derived` false and replays `chain` looking for a
        /// conflict, exactly as an LRAT replay checker would.
        [[nodiscard]] bool verify_chain(const std::vector<literal>& derived, const std::vector<clause::id::value_t>& chain) const noexcept;

        clause_literal_map_t clauses_ {};
        antecedent_map_t antecedents_ {};
    };
}
