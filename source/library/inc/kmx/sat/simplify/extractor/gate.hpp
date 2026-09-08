/// @file inc/kmx/sat/simplify/extractor/gate.hpp
/// @brief Detects exploitable logical structures (AND/XOR/ITE/definition gates).
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <array>
    #include <cstddef>
    #include <cstdint>
    #include <unordered_set>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/literal.hpp>

namespace kmx::sat::simplify::extractor
{
    /// @brief Detects exploitable logical structures (AND/XOR/ITE/definition gates).
    /// @details
    /// Many CNF instances are Tseitin-encoded from a higher-level circuit; `extractor::gate` recovers that structure
    /// by pattern-matching clause groups around one variable: `find_and_gate`/`find_xor_gate`/`find_ite_gate` detect
    /// the clause patterns characteristic of AND, XOR, and if-then-else gate encodings respectively, and
    /// `find_definition_gate` detects the general case where a variable is functionally defined by its neighboring
    /// clauses without matching a specific gate shape. `materialize_gate_summary` packages the discovered structure
    /// for `engine::congruence` to build constraints from and for `factorizer`/`sweep_engine` to exploit.
    /// @note Whether these gates can be reported to `proof::proof_manager` natively (as gate/XOR-level events) or
    /// must be expanded into resolution-equivalent events depends on the currently active proof format, per the
    /// proof-format compatibility matrix: only `veripb_tracer` supports native gate-level reasoning in the baseline.
    class gate final
    {
    public:
        enum class gate_kind : std::uint8_t
        {
            and_gate,
            xor_gate,
            ite_gate,
            definition_gate
        };

        struct gate_record final
        {
            gate_kind kind {gate_kind::definition_gate};
            std::uint32_t output {};
            std::array<std::uint32_t, 2u> inputs {0u, 0u};
        };

        /// @brief Constructs a gate extractor with no cached candidate structures.
        /// @throws None (noexcept).
        gate() noexcept = default;

        /// @brief Attaches clause storage used for pattern-based gate discovery.
        /// @param database Clause database to scan.
        /// @throws None (noexcept).
        void attach_clause_database(cdcl::clause::database& database) noexcept { clause_database_ = &database; }

        /// @brief Enables or disables synthetic gate discovery used by lightweight tests.
        /// @param enabled True to allow no-argument discovery calls to emit synthetic gate records.
        /// @throws None (noexcept).
        void set_synthetic_discovery_enabled(const bool enabled) noexcept { synthetic_discovery_enabled_ = enabled; }

        /// @brief Returns whether synthetic no-argument gate discovery is enabled.
        /// @return True if synthetic discovery is active.
        bool synthetic_discovery_enabled() const noexcept { return synthetic_discovery_enabled_; }

        /// @brief Searches for AND-gate clause patterns around candidate variables.
        /// @throws None (noexcept).
        void find_and_gate() noexcept;

        /// @brief Records an AND gate candidate discovered by the caller.
        /// @param output Candidate gate output variable id.
        /// @param lhs First gate input variable id.
        /// @param rhs Second gate input variable id.
        /// @throws None (noexcept).
        void find_and_gate(const std::uint32_t output, const std::uint32_t lhs, const std::uint32_t rhs) noexcept
        {
            add_gate(gate_kind::and_gate, output, {lhs, rhs});
        }

        /// @brief Searches for XOR-gate clause patterns around candidate variables.
        /// @throws None (noexcept).
        void find_xor_gate() noexcept;

        /// @brief Records an XOR gate candidate discovered by the caller.
        /// @param output Candidate gate output variable id.
        /// @param lhs First gate input variable id.
        /// @param rhs Second gate input variable id.
        /// @throws None (noexcept).
        void find_xor_gate(const std::uint32_t output, const std::uint32_t lhs, const std::uint32_t rhs) noexcept
        {
            add_gate(gate_kind::xor_gate, output, {lhs, rhs});
        }

        /// @brief Searches for if-then-else-gate clause patterns around candidate variables.
        /// @throws None (noexcept).
        void find_ite_gate() noexcept;

        /// @brief Records an ITE gate candidate discovered by the caller.
        /// @param output Candidate gate output variable id.
        /// @param cond Condition variable id.
        /// @param data Data variable id.
        /// @throws None (noexcept).
        void find_ite_gate(const std::uint32_t output, const std::uint32_t cond, const std::uint32_t data) noexcept
        {
            add_gate(gate_kind::ite_gate, output, {cond, data});
        }

        /// @brief Searches for general functional-definition clause patterns not matching a specific gate shape.
        /// @throws None (noexcept).
        void find_definition_gate() noexcept;

        /// @brief Records a definition-gate candidate discovered by the caller.
        /// @param output Candidate defined variable id.
        /// @param lhs First dependency variable id.
        /// @param rhs Second dependency variable id.
        /// @throws None (noexcept).
        void find_definition_gate(const std::uint32_t output, const std::uint32_t lhs, const std::uint32_t rhs) noexcept
        {
            add_gate(gate_kind::definition_gate, output, {lhs, rhs});
        }

        /// @brief Packages discovered gate structures into a summary consumed by downstream passes.
        /// @throws None (noexcept).
        void materialize_gate_summary() noexcept
        {
            summary_materialized_ = true;
            summarized_gate_count_ = gate_records_.size();
        }

        std::size_t gate_count() const noexcept { return gate_records_.size(); }

        std::size_t summarized_gate_count() const noexcept { return summarized_gate_count_; }

        bool summary_materialized() const noexcept { return summary_materialized_; }

        const std::vector<gate_record>& gate_records() const noexcept { return gate_records_; }

        void clear() noexcept;

    private:
        static std::array<std::uint32_t, 2u> normalize_inputs(const gate_kind kind, const std::array<std::uint32_t, 2u> inputs) noexcept;

        static std::uint64_t implication_key(const std::uint32_t antecedent, const std::uint32_t output) noexcept
        {
            return (static_cast<std::uint64_t>(antecedent) << 32u) | output;
        }

        bool has_binary_implication_clause(const std::unordered_set<std::uint64_t>& binary_implications, const std::uint32_t antecedent,
                                           const std::uint32_t output) const noexcept
        {
            return binary_implications.contains(implication_key(antecedent, output));
        }

        void collect_clauses(clause_list_t& out) const noexcept;

        void discover_and_gates_from_clause_database() noexcept;

        void add_gate(const gate_kind kind, const std::uint32_t output, const std::array<std::uint32_t, 2u> inputs) noexcept;

        cdcl::clause::database* clause_database_ {};
        std::vector<gate_record> gate_records_ {};
        std::size_t summarized_gate_count_ {};
        std::uint32_t next_synthetic_output_ {1u};
        std::uint32_t next_synthetic_input_ {101u};
        bool synthetic_discovery_enabled_ {};
        bool summary_materialized_ {};
    };
}
