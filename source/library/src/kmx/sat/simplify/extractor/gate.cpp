/// @file library/src/kmx/sat/simplify/extractor/gate.cpp
/// @brief Out-of-line definitions declared by kmx/sat/simplify/extractor/gate.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/simplify/extractor/gate.hpp>

namespace kmx::sat::simplify::extractor
{
    void gate::find_and_gate() noexcept
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

    void gate::find_xor_gate() noexcept
    {
        if (!synthetic_discovery_enabled_)
            return;
        add_gate(gate_kind::xor_gate, next_synthetic_output_++, {next_synthetic_input_++, next_synthetic_input_++});
    }

    void gate::find_ite_gate() noexcept
    {
        if (!synthetic_discovery_enabled_)
            return;
        add_gate(gate_kind::ite_gate, next_synthetic_output_++, {next_synthetic_input_++, next_synthetic_input_++});
    }

    void gate::find_definition_gate() noexcept
    {
        if (!synthetic_discovery_enabled_)
            return;
        add_gate(gate_kind::definition_gate, next_synthetic_output_++, {next_synthetic_input_++, next_synthetic_input_++});
    }

    void gate::clear() noexcept
    {
        gate_records_.clear();
        summarized_gate_count_ = 0u;
        summary_materialized_ = false;
    }

    std::array<std::uint32_t, 2u> gate::normalize_inputs(const gate_kind kind, const std::array<std::uint32_t, 2u> inputs) noexcept
    {
        if ((kind != gate_kind::and_gate) && (kind != gate_kind::xor_gate))
            return inputs;

        if (inputs[0u] <= inputs[1u])
            return inputs;

        return {inputs[1u], inputs[0u]};
    }

    void gate::collect_clauses(clause_list_t& out) const noexcept
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

    void gate::discover_and_gates_from_clause_database() noexcept
    {
        clause_list_t clauses {};
        collect_clauses(clauses);
        if (clauses.empty())
            return;

        clause_list_t ternary_clauses {};
        std::unordered_set<std::uint64_t> binary_implications {};
        for (const auto& clause: clauses)
        {
            if (clause.size() == 2u)
            {
                const auto& first = clause[0u];
                const auto& second = clause[1u];
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
            std::array<std::uint32_t, 2u> negative_inputs {0u, 0u};
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

            if ((output == 0u) || (negative_count != 2u))
                continue;

            const bool has_left = has_binary_implication_clause(binary_implications, negative_inputs[0u], output);
            const bool has_right = has_binary_implication_clause(binary_implications, negative_inputs[1u], output);
            if (!has_left || !has_right)
                continue;

            add_gate(gate_kind::and_gate, output, negative_inputs);
        }
    }

    void gate::add_gate(const gate_kind kind, const std::uint32_t output, const std::array<std::uint32_t, 2u> inputs) noexcept
    {
        if ((output == 0u) || (inputs[0u] == 0u) || (inputs[1u] == 0u))
            return;

        const auto normalized_inputs = normalize_inputs(kind, inputs);
        const auto duplicate = std::find_if(gate_records_.begin(), gate_records_.end(),
                                            [&](const gate_record& existing) noexcept
                                            {
                                                return (existing.kind == kind) && (existing.output == output) &&
                                                       (normalize_inputs(existing.kind, existing.inputs) == normalized_inputs);
                                            });
        if (duplicate != gate_records_.end())
            return;

        gate_records_.push_back(gate_record {kind, output, normalized_inputs});
        summary_materialized_ = false;
    }
}
