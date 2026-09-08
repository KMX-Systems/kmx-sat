/// @file library/src/kmx/sat/simplify/scheduler/preprocess.cpp
/// @brief Out-of-line definitions declared by kmx/sat/simplify/scheduler/preprocess.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/simplify/scheduler/preprocess.hpp>

namespace kmx::sat::simplify::scheduler
{
    preprocess::preprocess() noexcept
    {
        decomposition_.attach_substitutor(decomposition_substitutor_);
        for (const auto id: baseline_passes)
        {
            const auto index = static_cast<std::size_t>(id);
            enabled_pass_mask_ |= (std::uint64_t {1u} << index);
            ++enabled_pass_count_;
        }
    }

    void preprocess::set_enabled_pass_mask(const std::uint64_t mask) noexcept
    {
        enabled_pass_mask_ = mask;
        enabled_pass_count_ = 0u;
        for (std::uint8_t value {}; value < pass_count; ++value)
            if ((enabled_pass_mask_ & (std::uint64_t {1u} << value)) != 0u)
                ++enabled_pass_count_;
    }

    void preprocess::attach_clause_database(cdcl::clause::database& database) noexcept
    {
        clause_database_ = &database;
        transitive_reducer_.attach_database(database);
        probing_.attach_database(database);
        forward_subsumer_.attach_database(database);
        vivifier_.attach_database(database);
        backbone_.attach_database(database);
        decomposition_substitutor_.attach_clause_database(database);
        congruence_.attach_clause_database(database);
        profile_selector_.attach_clause_database(database);
        bounded_.attach_clause_database(database);
        factorizer_.attach_clause_database(database);
    }

    void preprocess::attach_clause_sink(extractor::backbone::clause_sink_t sink) noexcept
    {
        backbone_.attach_clause_sink(sink);
        bounded_.attach_clause_sink(sink);
        forward_subsumer_.attach_clause_sink(sink);
    }

    void preprocess::attach_proof_manager(kmx::sat::proof_manager& proof_manager) noexcept
    {
        proof_manager_ = &proof_manager;
        transitive_reducer_.attach_proof_manager(proof_manager);
        probing_.attach_proof_manager(proof_manager);
        forward_subsumer_.attach_proof_manager(proof_manager);
        backbone_.attach_proof_manager(proof_manager);
        bounded_.attach_proof_manager(proof_manager);
        factorizer_.attach_proof_manager(proof_manager);
    }

    void preprocess::run_initial_pipeline() noexcept
    {
        ++pipeline_run_count_;
        executed_pass_count_ = 0u;
        last_run_summaries_.clear();
        backbone_.reset();
        sweep_.reset();

        profile_selector_.fingerprint_formula();
        profile_selector_.set_soft_memory_pressure((memory_governor_ != nullptr) && memory_governor_->soft_limit_breached());
        profile_selector_.select_pass_plan();

        for (std::uint8_t value {}; value < pass_count; ++value)
        {
            const auto id = static_cast<pass_id_t>(value);
            if (!is_enabled(id))
                continue;
            if (should_abort_pipeline())
                break;

            if (!profile_selector_.should_run_pass(id))
            {
                last_run_summaries_.push_back(pass_summary {
                    id,
                    false,
                    true,
                    false,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                });
                continue;
            }

            if (!is_proof_format_compatible(id))
            {
                last_run_summaries_.push_back(pass_summary {
                    id,
                    false,
                    false,
                    true,
                    std::nullopt,
                    std::nullopt,
                    std::nullopt,
                });
                continue;
            }

            const auto clause_count_before = clause_count_snapshot();
            run_pass(id);
            const auto clause_count_after = clause_count_snapshot();
            ++executed_pass_count_;

            std::optional<bool> pass_was_effective {std::nullopt};
            if (clause_count_before.has_value() && clause_count_after.has_value())
                pass_was_effective = *clause_count_after < *clause_count_before;
            profile_selector_.record_pass_effectiveness(id, pass_was_effective);
            last_run_summaries_.push_back(pass_summary {
                id,
                true,
                false,
                false,
                clause_count_before,
                clause_count_after,
                pass_was_effective,
            });
        }

        profile_selector_.update_selection_policy();
    }

    void preprocess::enable_pass(const pass_id_t id) noexcept
    {
        const auto index = static_cast<std::size_t>(id);
        const auto bit = std::uint64_t {1u} << index;
        if ((index < pass_count) && ((enabled_pass_mask_ & bit) == 0u))
        {
            enabled_pass_mask_ |= bit;
            ++enabled_pass_count_;
        }
    }

    void preprocess::disable_pass(const pass_id_t id) noexcept
    {
        const auto index = static_cast<std::size_t>(id);
        if (index < pass_count)
        {
            const auto bit = std::uint64_t {1u} << index;
            if ((enabled_pass_mask_ & bit) != 0u)
            {
                enabled_pass_mask_ &= ~bit;
                --enabled_pass_count_;
            }
        }
    }

    std::optional<std::size_t> preprocess::clause_count_snapshot() const noexcept
    {
        if (clause_database_ == nullptr)
            return {};

        const auto stats = clause_database_->stats_snapshot();
        return stats.irredundant_count + stats.redundant_count;
    }

    bool preprocess::is_proof_format_compatible(const pass_id_t id) const noexcept
    {
        if ((proof_manager_ == nullptr) || !proof_manager_->has_enabled_formats())
            return true;

        if ((id != pass_id_t::gate) && (id != pass_id_t::congruence))
            return true;

        // Conservative gating: these passes currently rely on native gate-level reasoning support.
        return proof_manager_->has_enabled_format(proof::format_id::veripb);
    }

    void preprocess::run_pass(const pass_id_t id) noexcept
    {
        switch (id)
        {
            case pass_id_t::transitive_reducer:
                transitive_reducer_.run();
                transitive_reducer_.prune_binary_edges();
                transitive_reducer_.report_removed_edges();
                return;
            case pass_id_t::decomposition:
                decomposition_.run_scc();
                decomposition_.find_equivalences();
                decomposition_.emit_substitutions();
                decomposition_substitutor_.rewrite_clauses();
                decomposition_substitutor_.rewrite_watches();
                decomposition_substitutor_.rewrite_external_mapping();
                return;
            case pass_id_t::probing:
                probing_.run_failed_literal_probing();
                for (const auto candidate: probing_.backbone_candidates())
                {
                    backbone_.record_candidate(candidate);
                    backbone_.confirm_candidate(candidate);
                    backbone_.emit_unit_fact(candidate);
                }
                return;
            case pass_id_t::forward_subsumer:
                forward_subsumer_.run();
                return;
            case pass_id_t::blocked:
                blocked_.run();
                return;
            case pass_id_t::covered:
                covered_.run();
                return;
            case pass_id_t::bounded:
                bounded_.run();
                return;
            case pass_id_t::fast:
                fast_.run_fast_round();
                return;
            case pass_id_t::instantiation:
                instantiation_.run();
                return;
            case pass_id_t::factorizer:
                factorizer_.run();
                return;
            case pass_id_t::gate:
            {
                auto& gate_extractor = congruence_.gate_extractor();
                gate_extractor.clear();
                gate_extractor.find_and_gate();
                gate_extractor.find_xor_gate();
                gate_extractor.find_ite_gate();
                gate_extractor.find_definition_gate();
                gate_extractor.materialize_gate_summary();
                return;
            }
            case pass_id_t::congruence:
                congruence_.run();
                return;
            case pass_id_t::vivifier:
                vivifier_.run();
                return;
            case pass_id_t::sweep:
                sweep_.build_micro_instance();
                sweep_.run();
                sweep_.extract_backbone();
                sweep_.extract_equivalences();
                sweep_.transfer_facts();
                return;
        }
        /* unreachable: every pass_id_t is handled above */
        return;
    }
}
