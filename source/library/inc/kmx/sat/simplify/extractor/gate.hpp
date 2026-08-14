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
    ///
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
            std::array<std::uint32_t, 2> inputs {0u, 0u};
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
        void find_and_gate() noexcept
        {
            if (clause_database_ != nullptr)
            {
                discover_and_gates_from_clause_database();
                return;
            }

            if (!synthetic_discovery_enabled_)
                return;
            add_gate(gate_kind::and_gate, next_synthetic_output_++, {next_synthetic_input_++, next_synthetic_input_++});
        }

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
        void find_xor_gate() noexcept
        {
            if (!synthetic_discovery_enabled_)
                return;
            add_gate(gate_kind::xor_gate, next_synthetic_output_++, {next_synthetic_input_++, next_synthetic_input_++});
        }

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
        void find_ite_gate() noexcept
        {
            if (!synthetic_discovery_enabled_)
                return;
            add_gate(gate_kind::ite_gate, next_synthetic_output_++, {next_synthetic_input_++, next_synthetic_input_++});
        }

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
        void find_definition_gate() noexcept
        {
            if (!synthetic_discovery_enabled_)
                return;
            add_gate(gate_kind::definition_gate, next_synthetic_output_++, {next_synthetic_input_++, next_synthetic_input_++});
        }

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

        void clear() noexcept
        {
            gate_records_.clear();
            summarized_gate_count_ = 0u;
            summary_materialized_ = false;
        }

    private:
        static std::array<std::uint32_t, 2> normalize_inputs(const gate_kind kind, const std::array<std::uint32_t, 2> inputs) noexcept
        {
            if (kind != gate_kind::and_gate && kind != gate_kind::xor_gate)
                return inputs;

            if (inputs[0] <= inputs[1])
                return inputs;

            return {inputs[1], inputs[0]};
        }

        static std::uint64_t implication_key(const std::uint32_t antecedent, const std::uint32_t output) noexcept
        {
            return (static_cast<std::uint64_t>(antecedent) << 32u) | output;
        }

        bool has_binary_implication_clause(const std::unordered_set<std::uint64_t>& binary_implications,
                                           const std::uint32_t antecedent, const std::uint32_t output) const noexcept
        {
            return binary_implications.contains(implication_key(antecedent, output));
        }

        void collect_clauses(std::vector<std::vector<literal>>& out) const noexcept
        {
            if (clause_database_ == nullptr)
                return;

            out.clear();
            out.reserve(clause_database_->stats_snapshot().irredundant_count + clause_database_->stats_snapshot().redundant_count);

            const auto collect_one = [&](const cdcl::clause::ref_t ref) noexcept
            {
                if (!ref.valid() || clause_database_->is_garbage(ref))
                    return;
                out.push_back(clause_database_->storage_of().literals_of(ref));
            };

            clause_database_->iterate_irredundant(collect_one);
            clause_database_->iterate_redundant(collect_one);
        }

        void discover_and_gates_from_clause_database() noexcept
        {
            std::vector<std::vector<literal>> clauses {};
            collect_clauses(clauses);
            if (clauses.empty())
                return;

            std::vector<std::vector<literal>> ternary_clauses {};
            std::unordered_set<std::uint64_t> binary_implications {};
            for (const auto& clause: clauses)
            {
                if (clause.size() == 2u)
                {
                    const auto& first = clause[0];
                    const auto& second = clause[1];
                    if (!first.is_negated() && second.is_negated())
                        binary_implications.insert(implication_key(first.variable_of().index(), second.variable_of().index()));
                    else if (first.is_negated() && !second.is_negated())
                        binary_implications.insert(implication_key(second.variable_of().index(), first.variable_of().index()));
                }
                else if (clause.size() == 3u)
                    ternary_clauses.push_back(clause);
            }

            for (const auto& clause: ternary_clauses)
            {
                std::uint32_t output {};
                std::array<std::uint32_t, 2> negative_inputs {0u, 0u};
                std::size_t negative_count {};

                for (const auto lit: clause)
                {
                    if (lit.is_negated())
                    {
                        if (negative_count < negative_inputs.size())
                            negative_inputs[negative_count++] = lit.variable_of().index();
                        continue;
                    }

                    if (output != 0u)
                    {
                        output = 0u;
                        break;
                    }
                    output = lit.variable_of().index();
                }

                if (output == 0u || negative_count != 2u)
                    continue;

                const bool has_left = has_binary_implication_clause(binary_implications, negative_inputs[0], output);
                const bool has_right = has_binary_implication_clause(binary_implications, negative_inputs[1], output);
                if (!has_left || !has_right)
                    continue;

                add_gate(gate_kind::and_gate, output, negative_inputs);
            }
        }

        void add_gate(const gate_kind kind, const std::uint32_t output, const std::array<std::uint32_t, 2> inputs) noexcept
        {
            if (output == 0u || inputs[0] == 0u || inputs[1] == 0u)
                return;

            const auto normalized_inputs = normalize_inputs(kind, inputs);
            const auto duplicate = std::find_if(gate_records_.begin(), gate_records_.end(),
                                                [&](const gate_record& existing) noexcept {
                                                    return existing.kind == kind && existing.output == output &&
                                                           normalize_inputs(existing.kind, existing.inputs) == normalized_inputs;
                                                });
            if (duplicate != gate_records_.end())
                return;

            gate_records_.push_back(gate_record {kind, output, normalized_inputs});
            summary_materialized_ = false;
        }

        cdcl::clause::database* clause_database_ {};
        std::vector<gate_record> gate_records_ {};
        std::size_t summarized_gate_count_ {};
        std::uint32_t next_synthetic_output_ {1u};
        std::uint32_t next_synthetic_input_ {101u};
        bool synthetic_discovery_enabled_ {};
        bool summary_materialized_ {};
    };
}
