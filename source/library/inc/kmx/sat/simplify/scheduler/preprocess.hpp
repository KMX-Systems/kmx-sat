/// @file inc/kmx/sat/simplify/scheduler/preprocess.hpp
/// @brief Simplification phases that run before the main search.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <array>
    #include <cstddef>
    #include <optional>
    #include <string_view>
    #include <vector>
#endif
#include <kmx/sat/cdcl/bank/watch_list.hpp>
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/memory_governor.hpp>
#include <kmx/sat/cdcl/variable_mapper.hpp>
#include <kmx/sat/proof_manager.hpp>
#include <kmx/sat/simplify/eliminator/clause/blocked.hpp>
#include <kmx/sat/simplify/eliminator/clause/covered.hpp>
#include <kmx/sat/simplify/eliminator/variable/bounded.hpp>
#include <kmx/sat/simplify/eliminator/variable/fast.hpp>
#include <kmx/sat/simplify/engine/congruence.hpp>
#include <kmx/sat/simplify/engine/decomposition.hpp>
#include <kmx/sat/simplify/engine/instantiation.hpp>
#include <kmx/sat/simplify/engine/probing.hpp>
#include <kmx/sat/simplify/engine/sweep.hpp>
#include <kmx/sat/simplify/equivalence_substitutor.hpp>
#include <kmx/sat/simplify/extractor/backbone.hpp>
#include <kmx/sat/simplify/extractor/gate.hpp>
#include <kmx/sat/simplify/factorizer.hpp>
#include <kmx/sat/simplify/forward_subsumer.hpp>
#include <kmx/sat/simplify/preprocessing_profile_selector.hpp>
#include <kmx/sat/simplify/transitive_reducer.hpp>
#include <kmx/sat/simplify/vivifier.hpp>

namespace kmx::sat::simplify::scheduler
{
    /// @brief Simplification phases that run before the main search.
    ///
    /// @details
    /// `scheduler::preprocess` runs the fixed pipeline of one-time simplification passes
    /// (`transitive_reducer`, `engine::decomposition`, `engine::probing`, `forward_subsumer`,
    /// `eliminator::clause::blocked`/`covered`, `eliminator::variable::bounded`/`fast`, `engine::instantiation`,
    /// `factorizer`, `extractor::gate`, `engine::congruence`, `vivifier`, `engine::sweep`, `extractor::backbone`)
    /// exactly once before `search_coordinator` begins, per the Phase 8 preprocess pipeline.
    /// `run_initial_pipeline` drives that sequence; `enable_pass`/`disable_pass` let a `solve_request::enabled_pass_mask`
    /// or `preprocessing_profile_selector` decision include/exclude individual passes by name;
    /// `should_abort_pipeline` checks the caller's termination callback and `memory_governor` ceilings between
    /// passes; `report_pass_summary` feeds `telemetry::solver_statistics` with per-pass effectiveness for
    /// diagnostics and for `preprocessing_profile_selector`'s future scheduling decisions.
    /// @note Per the format-compatibility enforcement rule, this scheduler must query `proof::proof_manager` for the
    /// currently enabled proof format(s) before running a format-sensitive pass, falling back to a resolution-only
    /// strategy or skipping the pass when no compatible representation is available.
    class preprocess final
    {
    public:
        struct pass_summary final
        {
            std::string_view pass_name {};
            bool executed {false};
            bool skipped_by_selector {false};
            bool skipped_by_proof_format {false};
            std::optional<std::size_t> clause_count_before {};
            std::optional<std::size_t> clause_count_after {};
            std::optional<bool> was_effective {};
        };

        static constexpr std::array<std::string_view, 13> baseline_passes {
            "transitive_reducer", "decomposition", "probing", "forward_subsumer", "blocked", "covered", "bounded", "fast",
            "instantiation",      "factorizer",    "gate",    "congruence",       "vivifier"};

        /// @brief Constructs a preprocess scheduler with every baseline pass enabled.
        /// @throws None (noexcept).
        preprocess() noexcept: enabled_passes_ {baseline_passes.begin(), baseline_passes.end()}
        {
            decomposition_.attach_substitutor(decomposition_substitutor_);
        }

        /// @brief Attaches the clause database consumed by preprocess passes.
        /// @param database Clause database to simplify in place.
        /// @throws None (noexcept).
        void attach_clause_database(cdcl::clause::database& database) noexcept
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
        }

        void attach_clause_sink(extractor::backbone::clause_sink_t sink) noexcept { backbone_.attach_clause_sink(std::move(sink)); }

        /// @brief Attaches the watch-list bank consumed by equivalence-rewrite passes.
        /// @param watch_list Watch list to reindex during literal substitution.
        /// @throws None (noexcept).
        void attach_watch_list(cdcl::bank::watch_list& watch_list) noexcept
        {
            decomposition_substitutor_.attach_watch_list(watch_list);
            congruence_.attach_watch_list(watch_list);
        }

        /// @brief Attaches the variable mapper consumed by equivalence-rewrite passes.
        /// @param mapper Variable mapper to update after substitutions.
        /// @throws None (noexcept).
        void attach_variable_mapper(cdcl::variable_mapper& mapper) noexcept
        {
            decomposition_substitutor_.attach_variable_mapper(mapper);
            congruence_.attach_variable_mapper(mapper);
        }

        /// @brief Attaches a memory governor whose hard-ceiling state can abort the pipeline.
        /// @param governor Memory governor to consult between passes.
        /// @throws None (noexcept).
        void attach_memory_governor(cdcl::memory_governor& governor) noexcept { memory_governor_ = &governor; }

        /// @brief Attaches the proof manager used for format-aware pass gating.
        /// @param proof_manager Proof manager describing the currently active proof formats.
        void attach_proof_manager(kmx::sat::proof_manager& proof_manager) noexcept
        {
            proof_manager_ = &proof_manager;
            transitive_reducer_.attach_proof_manager(proof_manager);
            probing_.attach_proof_manager(proof_manager);
            forward_subsumer_.attach_proof_manager(proof_manager);
            backbone_.attach_proof_manager(proof_manager);
        }

        /// @brief Requests that the current or next pipeline run abort before further passes.
        /// @throws None (noexcept).
        void request_abort() noexcept { abort_requested_ = true; }

        /// @brief Clears any explicit abort request.
        /// @throws None (noexcept).
        void clear_abort() noexcept { abort_requested_ = false; }

        /// @brief Runs the full one-time preprocessing pipeline before the main search begins.
        /// @throws None (noexcept).
        void run_initial_pipeline() noexcept
        {
            ++pipeline_run_count_;
            executed_pass_count_ = 0u;
            last_run_summaries_.clear();

            profile_selector_.fingerprint_formula();
            profile_selector_.set_soft_memory_pressure(memory_governor_ != nullptr && memory_governor_->soft_limit_breached());
            profile_selector_.select_pass_plan();

            for (const auto pass_name: enabled_passes_)
            {
                if (should_abort_pipeline())
                {
                    break;
                }

                if (!profile_selector_.should_run_pass(pass_name))
                {
                    last_run_summaries_.push_back(pass_summary {
                        pass_name,
                        false,
                        true,
                        false,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                    });
                    continue;
                }

                if (!is_proof_format_compatible(pass_name))
                {
                    last_run_summaries_.push_back(pass_summary {
                        pass_name,
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
                run_pass(pass_name);
                const auto clause_count_after = clause_count_snapshot();
                ++executed_pass_count_;

                std::optional<bool> pass_was_effective {std::nullopt};
                if (clause_count_before.has_value() && clause_count_after.has_value())
                {
                    pass_was_effective = *clause_count_after < *clause_count_before;
                }
                profile_selector_.record_pass_effectiveness(pass_name, pass_was_effective);
                last_run_summaries_.push_back(pass_summary {
                    pass_name,
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

        /// @brief Enables a named simplification pass for subsequent pipeline runs.
        /// @param pass_name Identifier of the pass to enable.
        /// @throws None (noexcept).
        void enable_pass(const std::string_view pass_name) noexcept
        {
            if (!is_known_pass(pass_name) || is_enabled(pass_name))
            {
                return;
            }
            enabled_passes_.push_back(pass_name);
        }

        /// @brief Disables a named simplification pass for subsequent pipeline runs.
        /// @param pass_name Identifier of the pass to disable.
        /// @throws None (noexcept).
        void disable_pass(const std::string_view pass_name) noexcept
        {
            enabled_passes_.erase(std::remove(enabled_passes_.begin(), enabled_passes_.end(), pass_name), enabled_passes_.end());
        }

        /// @brief Checks whether the pipeline should abort early due to termination or memory pressure.
        /// @return True if the pipeline should stop before running further passes.
        /// @throws None (noexcept).
        bool should_abort_pipeline() const noexcept
        {
            return abort_requested_ || (memory_governor_ != nullptr && memory_governor_->hard_limit_breached());
        }

        /// @brief Reports a summary of the last pipeline run's per-pass effectiveness to telemetry.
        /// @throws None (noexcept).
        void report_pass_summary() const noexcept
        {
            last_reported_summaries_ = last_run_summaries_;
            ++reported_summary_count_;
        }

        std::size_t enabled_pass_count() const noexcept { return enabled_passes_.size(); }

        std::size_t executed_pass_count() const noexcept { return executed_pass_count_; }

        std::size_t pipeline_run_count() const noexcept { return pipeline_run_count_; }

        std::size_t reported_summary_count() const noexcept { return reported_summary_count_; }

        const std::vector<pass_summary>& last_reported_summaries() const noexcept { return last_reported_summaries_; }

        bool abort_requested() const noexcept { return abort_requested_; }

        simplify::engine::decomposition& decomposition_engine() noexcept { return decomposition_; }

        simplify::engine::congruence& congruence_engine() noexcept { return congruence_; }

        simplify::preprocessing_profile_selector& profile_selector() noexcept { return profile_selector_; }

        const simplify::preprocessing_profile_selector& profile_selector() const noexcept { return profile_selector_; }

    private:
        std::optional<std::size_t> clause_count_snapshot() const noexcept
        {
            if (clause_database_ == nullptr)
            {
                return std::nullopt;
            }

            const auto stats = clause_database_->stats_snapshot();
            return stats.irredundant_count + stats.redundant_count;
        }

        bool is_known_pass(const std::string_view pass_name) const noexcept
        {
            return std::find(baseline_passes.begin(), baseline_passes.end(), pass_name) != baseline_passes.end();
        }

        bool is_enabled(const std::string_view pass_name) const noexcept
        {
            return std::find(enabled_passes_.begin(), enabled_passes_.end(), pass_name) != enabled_passes_.end();
        }

        bool is_proof_format_compatible(const std::string_view pass_name) const noexcept
        {
            if (proof_manager_ == nullptr || !proof_manager_->has_enabled_formats())
            {
                return true;
            }

            if (pass_name != "gate" && pass_name != "congruence")
            {
                return true;
            }

            // Conservative gating: these passes currently rely on native gate-level reasoning support.
            return proof_manager_->has_enabled_format("veripb");
        }

        void run_pass(const std::string_view pass_name) noexcept
        {
            if (pass_name == "transitive_reducer")
            {
                transitive_reducer_.run();
                transitive_reducer_.prune_binary_edges();
                transitive_reducer_.report_removed_edges();
                return;
            }
            if (pass_name == "decomposition")
            {
                decomposition_.run_scc();
                decomposition_.find_equivalences();
                decomposition_.emit_substitutions();
                decomposition_substitutor_.rewrite_clauses();
                decomposition_substitutor_.rewrite_watches();
                decomposition_substitutor_.rewrite_external_mapping();
                return;
            }
            if (pass_name == "probing")
            {
                probing_.run_failed_literal_probing();
                for (const auto candidate: probing_.backbone_candidates())
                {
                    backbone_.record_candidate(candidate);
                    backbone_.confirm_candidate(candidate);
                    backbone_.emit_unit_fact(candidate);
                }
                return;
            }
            if (pass_name == "forward_subsumer")
            {
                forward_subsumer_.run();
                return;
            }
            if (pass_name == "blocked")
            {
                blocked_.run();
                return;
            }
            if (pass_name == "covered")
            {
                covered_.run();
                return;
            }
            if (pass_name == "bounded")
            {
                bounded_.run();
                return;
            }
            if (pass_name == "fast")
            {
                fast_.run_fast_round();
                return;
            }
            if (pass_name == "instantiation")
            {
                instantiation_.run();
                return;
            }
            if (pass_name == "factorizer")
            {
                factorizer_.run();
                return;
            }
            if (pass_name == "gate")
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
            if (pass_name == "congruence")
            {
                congruence_.run();
                return;
            }
            if (pass_name == "vivifier")
            {
                vivifier_.run();
            }
        }

        std::vector<std::string_view> enabled_passes_ {};
        cdcl::clause::database* clause_database_ {nullptr};
        cdcl::memory_governor* memory_governor_ {nullptr};
        kmx::sat::proof_manager* proof_manager_ {nullptr};
        bool abort_requested_ {false};
        mutable std::size_t reported_summary_count_ {0u};
        mutable std::vector<pass_summary> last_reported_summaries_ {};
        std::vector<pass_summary> last_run_summaries_ {};
        std::size_t executed_pass_count_ {0u};
        std::size_t pipeline_run_count_ {0u};
        simplify::transitive_reducer transitive_reducer_ {};
        simplify::equivalence_substitutor decomposition_substitutor_ {};
        simplify::engine::decomposition decomposition_ {};
        simplify::engine::probing probing_ {};
        simplify::forward_subsumer forward_subsumer_ {};
        simplify::eliminator::clause::blocked blocked_ {};
        simplify::eliminator::clause::covered covered_ {};
        simplify::eliminator::variable::bounded bounded_ {};
        simplify::eliminator::variable::fast fast_ {};
        simplify::engine::instantiation instantiation_ {};
        simplify::factorizer factorizer_ {};
        simplify::engine::congruence congruence_ {};
        simplify::preprocessing_profile_selector profile_selector_ {};
        simplify::vivifier vivifier_ {};
        engine::sweep sweep_ {};
        extractor::backbone backbone_ {};
    };
}
