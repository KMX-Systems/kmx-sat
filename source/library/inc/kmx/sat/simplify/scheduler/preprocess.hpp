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
#include <kmx/sat/cdcl/stack/extension.hpp>
#include <kmx/sat/simplify/forward_subsumer.hpp>
#include <kmx/sat/simplify/preprocessing_profile_selector.hpp>
#include <kmx/sat/simplify/transitive_reducer.hpp>
#include <kmx/sat/simplify/vivifier.hpp>

namespace kmx::sat::simplify::scheduler
{
    /// @brief Simplification phases that run before the main search.
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
        using pass_id = simplify::pass_id;

        struct pass_summary final
        {
            pass_id id {};
            bool executed {};
            bool skipped_by_selector {};
            bool skipped_by_proof_format {};
            std::optional<std::size_t> clause_count_before {};
            std::optional<std::size_t> clause_count_after {};
            std::optional<bool> was_effective {};
        };

        /// @brief Passes enabled by default on every pipeline run.
        /// @note `pass_id::bounded` is deliberately absent. Bounded variable elimination is implemented and is
        /// verified sound in isolation (satisfiability preservation plus model reconstruction, see
        /// `bounded_elimination_test`), but enabling it here makes the solver report satisfiable for
        /// unsatisfiable instances: learned clauses mentioning an eliminated variable are not removed with it, and
        /// eliminations are not scoped to an incremental epoch, so a later episode can constrain a variable that no
        /// longer exists. Enable it explicitly through `enable_pass` once those two interactions are handled.
        /// @details `bounded` is deliberately absent. Bounded variable elimination is what collapses the auxiliary
        /// variables of a Tseitin encoding -- enabling it takes a 1500-variable XOR chain from 0.16 s to 0.01 s --
        /// but eliminating a variable discards the clauses constraining it, and this class is used behind an
        /// incremental API. A clause added over an eliminated variable after a solve has nothing left to
        /// contradict it, and the next solve answers satisfiable on an unsatisfiable formula. Making it safe for
        /// incremental use needs the eliminated clauses restored when a later clause mentions the variable, which
        /// is not implemented. Callers that add every clause before solving once can opt in through
        /// `solve_request::enabled_pass_mask`; `baseline_pass_mask_with_variable_elimination` is that mask.
        /// @brief Passes enabled by default. `decomposition` (equivalent-literal substitution) is deliberately
        /// absent, like `bounded`: `equivalence_substitutor` merges variables without polarity and records no
        /// witness for model reconstruction, so a substitution can produce a model that violates the original
        /// formula (observed on `bmc-ibm-13`). It stays available by explicit enablement until that is fixed.
        /// `congruence` shares the substitutor and is absent for the same reason, and `gate` extraction, which
        /// only feeds congruence closure, goes with it: on a 114k-clause scheduling formula it was 3% of the
        /// preprocessing instructions spent copying clauses for a consumer that never ran.
        static constexpr std::array<pass_id, 9u> baseline_passes {pass_id::transitive_reducer,
                                                                  pass_id::probing,
                                                                  pass_id::forward_subsumer,
                                                                  pass_id::blocked,
                                                                  pass_id::covered,
                                                                  pass_id::fast,
                                                                  pass_id::instantiation,
                                                                  pass_id::factorizer,
                                                                  pass_id::vivifier};

        static constexpr std::array<std::string_view, 14> pass_names {
            "transitive_reducer", "decomposition", "probing", "forward_subsumer", "blocked",  "covered", "bounded", "fast",
            "instantiation",      "factorizer",    "gate",    "congruence",       "vivifier", "sweep"};

        static constexpr std::string_view pass_name(const pass_id id) noexcept
        {
            const auto index = static_cast<std::size_t>(id);
            return index < pass_names.size() ? pass_names[index] : std::string_view {};
        }

        /// @brief Returns the reporting label for an enum-backed pass summary.
        static constexpr std::string_view pass_name_of(const pass_summary& summary) noexcept { return pass_name(summary.id); }

        /// @brief Constructs a preprocess scheduler with every baseline pass enabled.
        /// @throws None (noexcept).
        preprocess() noexcept
        {
            decomposition_.attach_substitutor(decomposition_substitutor_);
            for (const auto id: baseline_passes)
            {
                const auto index = static_cast<std::size_t>(id);
                enabled_pass_mask_ |= (std::uint64_t {1u} << index);
                ++enabled_pass_count_;
            }
        }

        /// @brief Returns the bit representing one pass in an enabled-pass bitmask.
        static constexpr std::uint64_t pass_bit(const pass_id id) noexcept
        {
            return std::uint64_t {1u} << static_cast<std::uint8_t>(id);
        }

        /// @brief Returns the bitmask of the default pass set.
        static constexpr std::uint64_t baseline_pass_mask() noexcept
        {
            std::uint64_t mask {};
            for (const auto id: baseline_passes)
                mask |= pass_bit(id);
            return mask;
        }

        /// @brief Returns the default pass set plus bounded variable elimination.
        /// @details Only sound for a caller that states the whole problem before solving and never adds a clause
        /// afterwards; see the note on `baseline_passes`.
        static constexpr std::uint64_t baseline_pass_mask_with_variable_elimination() noexcept
        {
            return baseline_pass_mask() | pass_bit(pass_id::bounded);
        }

        /// @brief Replaces the enabled-pass set with an explicit bitmask, one bit per `pass_id`.
        /// @details Backs `solve_request::enabled_pass_mask`, so a caller can state exactly which simplification
        /// it wants -- including none. Tests of the search machinery need that: preprocessing is strong enough to
        /// close a small formula outright, and a test that means to exercise propagation should say so rather than
        /// depend on which passes happen to be in the default set.
        /// @param mask Bitmask of enabled passes; zero disables every pass.
        /// @throws None (noexcept).
        void set_enabled_pass_mask(const std::uint64_t mask) noexcept
        {
            enabled_pass_mask_ = mask;
            enabled_pass_count_ = 0u;
            for (std::uint8_t value {}; value < pass_count; ++value)
                if ((enabled_pass_mask_ & (std::uint64_t {1u} << value)) != 0u)
                    ++enabled_pass_count_;
        }

        /// @brief Returns the current enabled-pass bitmask.
        [[nodiscard]] std::uint64_t enabled_pass_mask() const noexcept { return enabled_pass_mask_; }

        /// @brief Marks variables that eliminating passes must leave alone.
        /// @param variables Variables the caller can still refer to after simplification.
        /// @throws None (noexcept).
        void set_frozen_variables(const std::span<const kmx::sat::variable> variables) noexcept
        {
            bounded_.set_frozen_variables(variables);
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
            bounded_.attach_clause_database(database);
            factorizer_.attach_clause_database(database);
        }

        /// @brief Attaches the extension stack that variable elimination and factoring record their changes on.
        /// @details Without this the eliminator cannot record what it removed, and a model of the reduced formula
        /// could not be repaired into a model of the original, so the pass stays inert until it is attached; the
        /// factorizer records the variables it introduces there so they are dropped from external models.
        /// @param extension_stack Journal receiving one record per eliminated variable.
        /// @throws None (noexcept).
        void attach_extension_stack(cdcl::stack::extension& extension_stack) noexcept
        {
            bounded_.attach_extension_stack(extension_stack);
            factorizer_.attach_extension_stack(extension_stack);
        }

        /// @brief Declares how many variables the problem owns, for passes that introduce fresh ones.
        void set_problem_variable_count(const variable::index_t count) noexcept { factorizer_.set_problem_variable_count(count); }

        void attach_clause_sink(extractor::backbone::clause_sink_t sink) noexcept
        {
            backbone_.attach_clause_sink(sink);
            bounded_.attach_clause_sink(sink);
            forward_subsumer_.attach_clause_sink(sink);
        }

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

        /// @brief Injects inprocess telemetry so preprocess pass planning can reuse search pressure signals.
        /// @param conflict_density_ema Conflict-density EMA from inprocess scheduler.
        /// @param structural_gain_ema Structural-gain EMA from inprocess scheduler.
        /// @param restart_pressure_ema Restart-pressure EMA from inprocess scheduler.
        /// @param reduction_pressure_ema Reduction-pressure EMA from inprocess scheduler.
        /// @param learned_clause_pressure_ema Learned-clause-pressure EMA from inprocess scheduler.
        void set_inprocess_telemetry_snapshot(const double conflict_density_ema, const double structural_gain_ema,
                                              const double restart_pressure_ema, const double reduction_pressure_ema,
                                              const double learned_clause_pressure_ema) noexcept
        {
            profile_selector_.set_inprocess_telemetry(conflict_density_ema, structural_gain_ema, restart_pressure_ema,
                                                      reduction_pressure_ema, learned_clause_pressure_ema);
        }

        /// @brief Attaches the proof manager used for format-aware pass gating.
        /// @param proof_manager Proof manager describing the currently active proof formats.
        void attach_proof_manager(kmx::sat::proof_manager& proof_manager) noexcept
        {
            proof_manager_ = &proof_manager;
            transitive_reducer_.attach_proof_manager(proof_manager);
            probing_.attach_proof_manager(proof_manager);
            forward_subsumer_.attach_proof_manager(proof_manager);
            backbone_.attach_proof_manager(proof_manager);
            bounded_.attach_proof_manager(proof_manager);
            factorizer_.attach_proof_manager(proof_manager);
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
            backbone_.reset();
            sweep_.reset();

            profile_selector_.fingerprint_formula();
            profile_selector_.set_soft_memory_pressure(memory_governor_ != nullptr && memory_governor_->soft_limit_breached());
            profile_selector_.select_pass_plan();

            for (std::uint8_t value {}; value < pass_count; ++value)
            {
                const auto id = static_cast<pass_id>(value);
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

        /// @brief Enables a named simplification pass for subsequent pipeline runs.
        /// @param pass_name Identifier of the pass to enable.
        /// @throws None (noexcept).
        void enable_pass(const std::string_view pass_name) noexcept
        {
            const auto id = pass_id_from_name(pass_name);
            if (!id.has_value())
                return;
            enable_pass(id.value());
        }

        void enable_pass(const pass_id id) noexcept
        {
            const auto index = static_cast<std::size_t>(id);
            const auto bit = std::uint64_t {1u} << index;
            if (index < pass_count && (enabled_pass_mask_ & bit) == 0u)
            {
                enabled_pass_mask_ |= bit;
                ++enabled_pass_count_;
            }
        }

        /// @brief Disables a named simplification pass for subsequent pipeline runs.
        /// @param pass_name Identifier of the pass to disable.
        /// @throws None (noexcept).
        void disable_pass(const std::string_view pass_name) noexcept
        {
            const auto id = pass_id_from_name(pass_name);
            if (id.has_value())
                disable_pass(id.value());
        }

        void disable_pass(const pass_id id) noexcept
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

        std::size_t enabled_pass_count() const noexcept { return enabled_pass_count_; }

        std::size_t executed_pass_count() const noexcept { return executed_pass_count_; }

        std::size_t pipeline_run_count() const noexcept { return pipeline_run_count_; }

        std::size_t reported_summary_count() const noexcept { return reported_summary_count_; }

        const std::vector<pass_summary>& last_reported_summaries() const noexcept { return last_reported_summaries_; }

        [[nodiscard]] std::size_t subsumed_clause_count() const noexcept { return forward_subsumer_.subsumed_count(); }

        bool abort_requested() const noexcept { return abort_requested_; }

        simplify::engine::decomposition& decomposition_engine() noexcept { return decomposition_; }

        simplify::engine::congruence& congruence_engine() noexcept { return congruence_; }

        simplify::preprocessing_profile_selector& profile_selector() noexcept { return profile_selector_; }

        const simplify::preprocessing_profile_selector& profile_selector() const noexcept { return profile_selector_; }

    private:
        static constexpr std::uint32_t pass_name_hash(const std::string_view name) noexcept
        {
            std::uint32_t hash {2166136261u};
            for (const auto character: name)
            {
                hash ^= static_cast<std::uint8_t>(character);
                hash *= 16777619u;
            }
            return hash;
        }

        static constexpr std::optional<pass_id> pass_id_from_name(const std::string_view name) noexcept
        {
            const auto hash = pass_name_hash(name);
            std::optional<pass_id> candidate {};
            switch (hash)
            {
                case 0xb70710c1u:
                    candidate = pass_id::transitive_reducer;
                    break;
                case 0xda4c4018u:
                    candidate = pass_id::decomposition;
                    break;
                case 0xd6a79702u:
                    candidate = pass_id::probing;
                    break;
                case 0x5ef40fb1u:
                    candidate = pass_id::forward_subsumer;
                    break;
                case 0x5a5d6eb3u:
                    candidate = pass_id::blocked;
                    break;
                case 0xf6358681u:
                    candidate = pass_id::covered;
                    break;
                case 0xff54b66au:
                    candidate = pass_id::bounded;
                    break;
                case 0x029402afu:
                    candidate = pass_id::fast;
                    break;
                case 0x9a6bf24au:
                    candidate = pass_id::instantiation;
                    break;
                case 0x587fb7f6u:
                    candidate = pass_id::factorizer;
                    break;
                case 0x1660eb12u:
                    candidate = pass_id::gate;
                    break;
                case 0x60fe7efau:
                    candidate = pass_id::congruence;
                    break;
                case 0x1171d8a3u:
                    candidate = pass_id::vivifier;
                    break;
                case 0x518432e3u:
                    candidate = pass_id::sweep;
                    break;
                default:
                    return {};
            }
            if (pass_name(candidate.value()) == name)
                return candidate;
            return {};
        }

        std::optional<std::size_t> clause_count_snapshot() const noexcept
        {
            if (clause_database_ == nullptr)
                return {};

            const auto stats = clause_database_->stats_snapshot();
            return stats.irredundant_count + stats.redundant_count;
        }

        bool is_enabled(const pass_id id) const noexcept
        {
            const auto index = static_cast<std::size_t>(id);
            return index < pass_count && (enabled_pass_mask_ & (std::uint64_t {1u} << index)) != 0u;
        }

        bool is_proof_format_compatible(const pass_id id) const noexcept
        {
            if (proof_manager_ == nullptr || !proof_manager_->has_enabled_formats())
                return true;

            if (id != pass_id::gate && id != pass_id::congruence)
                return true;

            // Conservative gating: these passes currently rely on native gate-level reasoning support.
            return proof_manager_->has_enabled_format("veripb");
        }

        void run_pass(const pass_id id) noexcept
        {
            switch (id)
            {
                case pass_id::transitive_reducer:
                    transitive_reducer_.run();
                    transitive_reducer_.prune_binary_edges();
                    transitive_reducer_.report_removed_edges();
                    return;
                case pass_id::decomposition:
                    decomposition_.run_scc();
                    decomposition_.find_equivalences();
                    decomposition_.emit_substitutions();
                    decomposition_substitutor_.rewrite_clauses();
                    decomposition_substitutor_.rewrite_watches();
                    decomposition_substitutor_.rewrite_external_mapping();
                    return;
                case pass_id::probing:
                    probing_.run_failed_literal_probing();
                    for (const auto candidate: probing_.backbone_candidates())
                    {
                        backbone_.record_candidate(candidate);
                        backbone_.confirm_candidate(candidate);
                        backbone_.emit_unit_fact(candidate);
                    }
                    return;
                case pass_id::forward_subsumer:
                    forward_subsumer_.run();
                    return;
                case pass_id::blocked:
                    blocked_.run();
                    return;
                case pass_id::covered:
                    covered_.run();
                    return;
                case pass_id::bounded:
                    bounded_.run();
                    return;
                case pass_id::fast:
                    fast_.run_fast_round();
                    return;
                case pass_id::instantiation:
                    instantiation_.run();
                    return;
                case pass_id::factorizer:
                    factorizer_.run();
                    return;
                case pass_id::gate:
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
                case pass_id::congruence:
                    congruence_.run();
                    return;
                case pass_id::vivifier:
                    vivifier_.run();
                    return;
                case pass_id::sweep:
                    sweep_.build_micro_instance();
                    sweep_.run();
                    sweep_.extract_backbone();
                    sweep_.extract_equivalences();
                    sweep_.transfer_facts();
                    return;
            }
            /* unreachable: every pass_id is handled above */
            return;
        }

        static constexpr std::size_t pass_count {static_cast<std::size_t>(pass_id::sweep) + 1u};
        std::uint64_t enabled_pass_mask_ {};
        std::size_t enabled_pass_count_ {};
        cdcl::clause::database* clause_database_ {};
        cdcl::memory_governor* memory_governor_ {};
        kmx::sat::proof_manager* proof_manager_ {};
        bool abort_requested_ {};
        mutable std::size_t reported_summary_count_ {};
        mutable std::vector<pass_summary> last_reported_summaries_ {};
        std::vector<pass_summary> last_run_summaries_ {};
        std::size_t executed_pass_count_ {};
        std::size_t pipeline_run_count_ {};
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
