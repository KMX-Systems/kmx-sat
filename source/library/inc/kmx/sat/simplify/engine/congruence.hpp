/// @file inc/kmx/sat/simplify/engine/congruence.hpp
/// @brief Congruence closure over extracted structures.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cstddef>
    #include <cstdint>
#endif
#include <kmx/sat/simplify/equivalence_substitutor.hpp>
#include <kmx/sat/simplify/extractor/gate.hpp>

namespace kmx::sat::simplify::engine
{
    /// @brief Congruence closure over extracted structures.
    /// @details
    /// Once `extractor::gate` identifies functional structures (AND/XOR/ITE/definition gates), `engine::congruence`
    /// treats each as an equality constraint between the gate's output literal and its logical definition, then
    /// computes the congruence closure over all such constraints (extending plain equivalent-literal substitution to
    /// structural, gate-level equivalence): `apply_gate_constraints` feeds the discovered gates in as constraints,
    /// `derive_equivalences` computes the closure to find literals that must agree by construction, and `run` drives
    /// one full pass, handing confirmed equivalences to the owned `equivalence_substitutor` for solver-wide rewriting.
    /// @reference Congruence closure over gate/definition structures, as used by CaDiCaL's congruence-closure-based
    /// simplification.
    class congruence final
    {
    public:
        /// @brief Constructs a congruence engine with embedded gate extractor and equivalence substitutor.
        /// @throws None (noexcept).
        congruence() noexcept = default;

        /// @brief Attaches clause database storage for equivalence-driven clause rewriting.
        /// @param database Clause database to rewrite.
        /// @throws None (noexcept).
        void attach_clause_database(cdcl::clause::database& database) noexcept
        {
            gate_.attach_clause_database(database);
            equivalence_substitutor_.attach_clause_database(database);
        }

        /// @brief Attaches watch-list storage for equivalence-driven watch rewriting.
        /// @param watch_list Watch list to rewrite.
        /// @throws None (noexcept).
        void attach_watch_list(cdcl::bank::watch_list& watch_list) noexcept { equivalence_substitutor_.attach_watch_list(watch_list); }

        /// @brief Attaches variable mapper for equivalence-driven external/internal mapping rewrites.
        /// @param mapper Variable mapper to update.
        /// @throws None (noexcept).
        void attach_variable_mapper(cdcl::variable_mapper& mapper) noexcept { equivalence_substitutor_.attach_variable_mapper(mapper); }

        /// @brief Runs a full congruence-closure pass: extract gates, derive equivalences, substitute.
        /// @throws None (noexcept).
        void run() noexcept
        {
            gate_.materialize_gate_summary();
            apply_gate_constraints();
            derive_equivalences();
            equivalence_substitutor_.rewrite_clauses();
            equivalence_substitutor_.rewrite_watches();
            equivalence_substitutor_.rewrite_external_mapping();
            run_completed_ = true;
        }

        /// @brief Feeds discovered gate structures in as equality constraints for the closure computation.
        /// @throws None (noexcept).
        void apply_gate_constraints() noexcept
        {
            gate_constraint_count_ = gate_.summary_materialized() ? gate_.summarized_gate_count() : gate_.gate_count();
        }

        /// @brief Computes the congruence closure over the applied gate constraints.
        /// @throws None (noexcept).
        void derive_equivalences() noexcept
        {
            equivalence_count_ = 0u;
            for (const auto& record: gate_.gate_records())
            {
                if (record.kind == extractor::gate::gate_kind::xor_gate || record.kind == extractor::gate::gate_kind::ite_gate)
                    continue;

                if (record.inputs[0] != record.inputs[1])
                    continue;

                const auto representative = std::min(record.inputs[0], record.inputs[1]);
                if (representative == 0u || record.output == 0u || record.output == representative)
                    continue;

                equivalence_substitutor_.apply_equivalence_class(record.output, representative);
                ++equivalence_count_;
            }
        }

        extractor::gate& gate_extractor() noexcept { return gate_; }

        const extractor::gate& gate_extractor() const noexcept { return gate_; }

        equivalence_substitutor& substitutor() noexcept { return equivalence_substitutor_; }

        const equivalence_substitutor& substitutor() const noexcept { return equivalence_substitutor_; }

        std::size_t gate_constraint_count() const noexcept { return gate_constraint_count_; }

        std::size_t equivalence_count() const noexcept { return equivalence_count_; }

        bool run_completed() const noexcept { return run_completed_; }

    private:
        extractor::gate gate_ {};
        equivalence_substitutor equivalence_substitutor_ {};
        std::size_t gate_constraint_count_ {};
        std::size_t equivalence_count_ {};
        bool run_completed_ {};
    };
}
