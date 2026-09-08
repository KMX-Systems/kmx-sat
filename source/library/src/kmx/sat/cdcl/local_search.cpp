/// @file library/src/kmx/sat/cdcl/local_search.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/local_search.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/local_search.hpp>

namespace kmx::sat::cdcl
{
    bool local_search::prepare(const clause::database& database, const std::uint32_t variable_count) noexcept
    {
        variable_count_ = variable_count;
        clause_literals_.clear();
        clause_offsets_.clear();
        best_unsatisfied_count_ = npos;
        best_assignment_.clear();
        maximum_clause_size_ = 0u;

        if (variable_count == 0u)
            return false;

        const auto& storage = database.storage_of();
        database.iterate_irredundant(
            [&](const clause::ref_t ref) noexcept
            {
                const auto literals = storage.view_literals(ref);
                if (literals.empty())
                    return;
                clause_offsets_.push_back(static_cast<std::uint32_t>(clause_literals_.size()));
                for (const auto lit: literals)
                    clause_literals_.push_back(lit.raw());
                maximum_clause_size_ = std::max<std::uint32_t>(maximum_clause_size_, static_cast<std::uint32_t>(literals.size()));
            });
        if (clause_offsets_.empty())
            return false;
        clause_offsets_.push_back(static_cast<std::uint32_t>(clause_literals_.size()));

        // Occurrence lists indexed by literal, built with a counting pass so the whole index is two contiguous
        // allocations rather than one vector per literal.
        const auto clause_count = clause_offsets_.size() - 1u;
        const auto literal_slots = (static_cast<std::size_t>(variable_count_) + 1u) * 2u;
        occurrence_starts_.assign(literal_slots + 1u, 0u);
        for (const auto raw: clause_literals_)
            ++occurrence_starts_[raw + 1u];
        for (std::size_t index = 1u; index < occurrence_starts_.size(); ++index)
            occurrence_starts_[index] += occurrence_starts_[index - 1u];
        occurrences_.assign(clause_literals_.size(), 0u);
        occurrence_cursor_.assign(occurrence_starts_.begin(), occurrence_starts_.end() - 1L);
        for (std::uint32_t index = 0u; index < clause_count; ++index)
            for (auto position = clause_offsets_[index]; position < clause_offsets_[index + 1u]; ++position)
                occurrences_[occurrence_cursor_[clause_literals_[position]]++] = index;

        true_counts_.assign(clause_count, 0u);
        critical_variables_.assign(clause_count, 0u);
        unsatisfied_position_.assign(clause_count, npos32);
        break_counts_.assign(static_cast<std::size_t>(variable_count_) + 1u, 0u);
        assignment_.assign(static_cast<std::size_t>(variable_count_) + 1u, 0u);
        return true;
    }

    [[gnu::noinline]] bool local_search::walk(const configuration& config, const std::size_t max_flips,
                                              const std::span<const std::uint8_t> initial_phase) noexcept
    {
        if (clause_offsets_.size() < 2u)
            return false;

        initialize_assignment(initial_phase);
        recompute_state();
        if (unsatisfied_.size() < best_unsatisfied_count_)
            record_best();
        if (config.kind == strategy::probsat)
            prepare_probabilities(config);

        std::size_t flips {};
        while (!unsatisfied_.empty() && (flips < max_flips))
        {
            const auto clause_index = unsatisfied_[next_random() % unsatisfied_.size()];
            const auto flipped = (config.kind == strategy::probsat) ? select_probsat(clause_index) : select_walksat(clause_index, config);
            flip_variable(flipped);
            ++flips;
            if (unsatisfied_.size() < best_unsatisfied_count_)
                record_best();
        }
        flip_count_ += flips;
        return unsatisfied_.empty();
    }

    bool local_search::run(const clause::database& database, const std::uint32_t variable_count, const std::size_t max_flips,
                           const std::span<const std::uint8_t> initial_phase) noexcept
    {
        if (!prepare(database, variable_count))
            return false;
        return walk(configuration {}, max_flips, initial_phase);
    }

    void local_search::initialize_assignment(const std::span<const std::uint8_t> initial_phase) noexcept
    {
        for (std::uint32_t index = 1u; index <= variable_count_; ++index)
            if (index < initial_phase.size())
                assignment_[index] = (initial_phase[index] != 0u) ? 1u : 0u;
            else
                assignment_[index] = static_cast<std::uint8_t>(next_random() & 1u);
    }

    void local_search::recompute_state() noexcept
    {
        const auto clause_count = clause_offsets_.size() - 1u;
        unsatisfied_.clear();
        std::fill(break_counts_.begin(), break_counts_.end(), 0u);
        for (std::uint32_t index = 0u; index < clause_count; ++index)
        {
            std::uint32_t satisfied {};
            std::uint32_t critical {};
            for (auto position = clause_offsets_[index]; position < clause_offsets_[index + 1u]; ++position)
            {
                const auto raw = clause_literals_[position];
                if (is_true(raw))
                {
                    ++satisfied;
                    critical = raw >> 1u;
                }
            }
            true_counts_[index] = satisfied;
            critical_variables_[index] = critical;
            if (satisfied == 1u)
                ++break_counts_[critical];
            if (satisfied == 0u)
            {
                unsatisfied_position_[index] = static_cast<std::uint32_t>(unsatisfied_.size());
                unsatisfied_.push_back(index);
            }
            else
                unsatisfied_position_[index] = npos32;
        }
    }

    void local_search::prepare_probabilities(const configuration& config) noexcept
    {
        probabilities_.resize(probability_table_size);
        for (std::size_t break_count = 0u; break_count < probability_table_size; ++break_count)
            probabilities_[break_count] = std::pow(config.break_epsilon + static_cast<double>(break_count), -config.break_exponent);
    }
}
