/// @file library/src/kmx/sat/cdcl/solver_core.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/solver_core.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/solver_core.hpp>

namespace kmx::sat::cdcl
{
    solver_core::solver_core() noexcept
    {
        proof_manager_.set_event_buffering(false);
        clause_database_.storage_of().set_proof_id_tracking(false);
        rebind_internal_views();
    }

    void solver_core::reset() noexcept
    {
        clause_database_ = {};
        clause_database_.storage_of().set_proof_id_tracking(false);
        search_coordinator_ = {};
        propagator_ = {};
        proof_manager_ = {};
        proof_manager_.set_event_buffering(false);
        incremental_context_ = {};
        memory_governor_ = {};
        watch_list_ = {};
        variable_mapper_ = {};
        clause_cold_ = {};
        flush_restore_manager_ = {};
        extension_stack_.clear_all();
        preprocess_scheduler_ = {};
        inprocess_scheduler_ = {};
        heap_ = {};
        saved_phase_.clear();
        unit_clause_refs_.clear();
        original_clause_count_ = 0u;
        max_problem_variable_ = 0u;
        internal_model_.clear();
        failed_core_.clear();
        reset_episode_counters();
        watch_list_.reset_diagnostics();
        status_ = status::unknown;
        rebind_internal_views();
    }

    solver_core::status solver_core::solve(const solve_request& request) noexcept
    {
        internal_model_.clear();
        failed_core_.clear();
        reset_episode_counters();
        clause_cold_.reset();

        incremental_context_.begin_solve_epoch();
        memory_governor_.reset_epoch_usage();
        clause_database_.clear_reason_clauses();
        preprocess_scheduler_.clear_abort();
        preprocess_scheduler_.set_inprocess_telemetry_snapshot(
            inprocess_scheduler_.conflict_density_ema(), inprocess_scheduler_.structural_gain_ema(),
            inprocess_scheduler_.restart_pressure_ema(), inprocess_scheduler_.reduction_pressure_ema(),
            inprocess_scheduler_.learned_clause_pressure_ema());
        // A caller-supplied pass set replaces the default one; zero keeps the default rather than meaning
        // "no passes", matching how `solve_request::enabled_pass_mask` is documented and defaulted.
        if (request.enabled_pass_mask != 0u)
            preprocess_scheduler_.set_enabled_pass_mask(request.enabled_pass_mask);

        // Anything the caller can still name after simplification must survive it. Assumption variables are
        // exactly that: eliminating one discards the clauses that constrain it, and the episode then reports
        // satisfiable with a model that contradicts the assumption it was given.
        frozen_variables_scratch_.clear();
        frozen_variables_scratch_.reserve(request.assumptions.size());
        for (const auto assumption: request.assumptions)
            frozen_variables_scratch_.push_back(assumption.variable_of());
        preprocess_scheduler_.set_frozen_variables(frozen_variables_scratch_);
        // One scan of the formula as stated serves the scheduler, the walker and the first phase alike.
        const auto stated_variable_count = find_max_variable(request.assumptions);
        preprocess_scheduler_.set_problem_variable_count(stated_variable_count);

        const auto finalize_epoch = [this](const status result) noexcept
        {
            status_ = result;
            incremental_context_.end_solve_epoch();
            incremental_context_.reset_transient_state();
            search_coordinator_.clear_variable_selectability_filter();
            synchronize_search_outcome(status_);
            return status_;
        };

        // The formula as stated gets the first word: the lucky check and the 2,000-conflict probe run on it
        // before any preprocessing, so an instance the search settles there never pays for the pipeline (the
        // scheduling and circuit instances of the classic set: 20-30 ms of passes for a search of a few
        // conflicts). Everything the probe learns, clauses, phases, activities, is kept for the second phase,
        // which is the old order: pipeline, walk, search. A request with its own limits keeps the old order
        // outright, so that the limits mean what they say.
        raw_probe_conflicts_ = 0u;
        raw_probe_ran_ = false;
        // Assumption solves keep the old order: the lucky check stands aside under assumptions, and an
        // incremental caller's episode should look exactly as it did.
        if (raw_probe_enabled && !proof_manager_.has_active_consumers() && request.assumptions.empty() && (request.conflict_limit == 0u) &&
            (request.decision_limit == 0u))
        {
            variable_count_ = stated_variable_count;
            initialize_search_state(request);
            entry_phase_snapshot_.assign(saved_phase_.begin(), saved_phase_.end());
            // The lucky check restores the phases it started from, and the probe search starts from them too.
            opening_phase_snapshot_.assign(saved_phase_.begin(), saved_phase_.end());
            heap_snapshot_ = heap_;
            const auto learned_before = clause_database_.redundant_refs().size();
            clause_database_.storage_of().snapshot_arena(arena_image_);
            opening_walk_pending_ = false;
            rebuild_propagation_state();
            root_refuted_ = false;
            solve_request probe_request = request;
            probe_request.conflict_limit = raw_probe_conflict_cap;
            raw_phase_active_ = true;
            search_coordinator_.apply_assumptions(probe_request);
            propagator_.reset_episode_state();
            propagator_.set_pending_assumption_count(request.assumptions.size());
            (void)propagator_.propagate_assumptions();
            // The budget is the search's alone: the lucky check's attempts, up to four assignments of every
            // variable, are not charged against it (charged, they took a third of it on `ssa2670-130`).
            const auto lucky = try_lucky_assignments(request, false);
            raw_probe_propagation_limit_ = propagation_assignment_count_ + database_literal_count_ * raw_probe_propagations_per_literal;
            status_ = lucky                                      ? status::satisfiable :
                      root_refuted_                              ? root_conflict() :
                      (raw_probe_propagations_per_literal == 0u) ? status::unknown :
                                                                   run_search(probe_request);
            raw_phase_active_ = false;
            raw_probe_propagation_limit_ = 0u;
            if (status_ != status::unknown)
            {
                if (status_ == status::satisfiable)
                    build_internal_model();
                (void)run_inprocess_if_due();
                return finalize_epoch(status_);
            }
            // The probe was only an early exit: everything it learned goes, so the second phase is the same
            // run it would have been without the probe. Carrying its clauses and activities into the
            // preprocessed formula had cost `hole9` and `par16` two to four times.
            raw_probe_conflicts_ = search_coordinator_.conflict_event_count();
            raw_probe_ran_ = true;
            backtrack(0u);
            clause_database_.clear_reason_clauses();
            // The database goes back byte for byte: the probe's learned clauses vanish, and the literal order
            // inside every clause, which propagation permutes as watches move, is what it was, so the second
            // phase watches the same pairs the search would have watched without the probe.
            clause_database_.discard_redundant_since(learned_before);
            clause_database_.storage_of().restore_arena(arena_image_);
            heap_ = heap_snapshot_;
            std::copy(entry_phase_snapshot_.begin(), entry_phase_snapshot_.end(), saved_phase_.begin());
            // Fresh episode counters before the second phase computes its schedules, so restarts, modes and
            // the walk fall exactly where they would have without the probe.
            search_coordinator_.apply_assumptions(request);
        }

        // The walker takes the formula as stated, before preprocessing rewrites it. Factoring replaces the
        // at-most-one clauses of a colouring instance with definitions of fresh variables, and on that formula
        // the walk that solves `gcp125_17` in a third of a second finds nothing in thirty. A model of the
        // original formula seeds the original variables' phases; the fresh variables follow by propagation.
        // It is prepared here, not at entry, so a formula the first phase decides never pays for it.
        local_search_variable_count_ = stated_variable_count;
        local_search_prepared_ =
            local_search_applicable(local_search_variable_count_) && local_search_.prepare(clause_database_, local_search_variable_count_);

        preprocess_scheduler_.run_initial_pipeline();
        preprocess_scheduler_.report_pass_summary();
        inprocess_scheduler_.clear_abort();

        variable_count_ = find_max_variable(request.assumptions);
        initialize_search_state(request);
        // The phases as they are before probing: the opening walk starts from them, and the lucky check
        // restores them, whether or not a walk is pending. (Restoring only when one was pending let the last
        // lucky attempt's phases leak into the search of every formula too large for an opening walk.)
        opening_phase_snapshot_.assign(saved_phase_.begin(), saved_phase_.end());
        opening_walk_pending_ = prepare_opening_walk();
        opening_walk_due_at_ = search_coordinator_.conflict_event_count() + local_search_opening_probe_conflicts;
        rebuild_propagation_state();
        root_refuted_ = false;
        const auto probe_units_before = probe_unit_count_ + probe_lifted_count_;
        probe_failed_literals();
        // Units from probing mean implication structure the walk cannot use and the search can: on such
        // formulas every walk stalled far from a model while the search finished soon after (`hanoi4`:
        // 718k flips at two unsatisfied clauses for a search that ends 230 conflicts after the walk starts;
        // `bmc-ibm-13`, `bw_large_d`: re-walks ending at 8-20 unsatisfied clauses). The formulas the walk
        // solves, random k-SAT and colouring, yield no units at all.
        if (walk_only_without_probe_units && (probe_unit_count_ + probe_lifted_count_ != probe_units_before))
        {
            opening_walk_pending_ = false;
            local_search_enabled_ = false;
        }

        search_coordinator_.apply_assumptions(request);
        propagator_.reset_episode_state();
        propagator_.set_pending_assumption_count(request.assumptions.size());
        (void)propagator_.propagate_assumptions();

        // A conflict found by root propagation before the search is consumed by the propagator (the trail
        // literal that exposed it counts as propagated), so it is reported here rather than rediscovered.
        status_ = root_refuted_ ? root_conflict() : try_lucky_assignments(request) ? status::satisfiable : run_search(request);

        if (status_ == status::satisfiable)
            build_internal_model();

        (void)run_inprocess_if_due();

        return finalize_epoch(status_);
    }

    solver_core::status solver_core::solve_under_assumptions(const std::span<const literal> assumptions) noexcept
    {
        solve_request request {};
        request.assumptions.assign(assumptions.begin(), assumptions.end());
        return solve(request);
    }

    void solver_core::reserve(const variable::index_t variable_bound) noexcept
    {
        if (variable_bound == 0u)
            return;
        watch_list_.reserve(static_cast<std::size_t>(variable_bound) * 2u + 2u);
        heap_.resize(variable_bound);
        saved_phase_.reserve(static_cast<std::size_t>(variable_bound) + 1u);
    }

    void solver_core::add_problem_clause(const std::span<const literal> literals) noexcept
    {
        ++original_clause_count_;
        for (const auto lit: literals)
            if (lit.variable_of().index() > max_problem_variable_)
                max_problem_variable_ = lit.variable_of().index();

        if (!normalize_clause(literals))
            return;
        const auto ref = clause_database_.add_clause(normalized_clause_scratch_, false);
        proof_manager_.on_add_original(ref, normalized_clause_scratch_);
    }

    void solver_core::attach_proof_tracer(const proof::tracer::view& sink) noexcept
    {
        auto& storage = clause_database_.storage_of();
        storage.enable_proof_ids(clause_database_.irredundant_refs());
        for (const auto ref: clause_database_.redundant_refs())
            if (storage.is_alive(ref))
                (void)storage.assign_proof_id(ref);
        proof_manager_.register_tracer(sink);
        // Ids for everything already in the database: events were not dispatched while no consumer existed.
        const auto adopt = [this](const clause::ref_t ref) noexcept { proof_manager_.adopt_clause(ref); };
        clause_database_.iterate_irredundant(adopt);
        clause_database_.iterate_redundant(adopt);
    }

    void solver_core::set_decision_maintenance_intervals(const std::uint32_t conflict_maintenance_interval,
                                                         const std::uint32_t chb_decay_interval,
                                                         const std::uint32_t restart_decay_interval) noexcept
    {
        evsids_maintenance_interval_ = conflict_maintenance_interval;
        search_coordinator_.set_decision_maintenance_intervals(conflict_maintenance_interval, chb_decay_interval, restart_decay_interval);
    }

    solver_core::inprocess_telemetry_snapshot solver_core::current_inprocess_telemetry_snapshot() const noexcept
    {
        return inprocess_telemetry_snapshot {
            inprocess_scheduler_.conflict_density_ema(),        inprocess_scheduler_.structural_gain_ema(),
            inprocess_scheduler_.restart_pressure_ema(),        inprocess_scheduler_.reduction_pressure_ema(),
            inprocess_scheduler_.learned_clause_pressure_ema(),
        };
    }

    void solver_core::rebind_internal_views() noexcept
    {
        search_coordinator_.attach_database(clause_database_);
        flush_restore_manager_.attach_database(clause_database_);
        preprocess_scheduler_.attach_memory_governor(memory_governor_);
        preprocess_scheduler_.attach_clause_database(clause_database_);
        preprocess_scheduler_.attach_watch_list(watch_list_);
        preprocess_scheduler_.attach_variable_mapper(variable_mapper_);
        preprocess_scheduler_.attach_proof_manager(proof_manager_);
        preprocess_scheduler_.attach_extension_stack(extension_stack_);
        model_reconstructor_.attach_extension_stack(extension_stack_);
        // Simplification passes announce new or rewritten clauses through this sink. Watches are rebuilt
        // wholesale after every pipeline and every epoch, so there is nothing for the sink to do.
        preprocess_scheduler_.attach_clause_sink([](const clause::ref_t) noexcept {});
        inprocess_scheduler_.attach_memory_governor(memory_governor_);
        inprocess_scheduler_.attach_clause_database(clause_database_);
        inprocess_scheduler_.attach_watch_list(watch_list_);
        inprocess_scheduler_.attach_variable_mapper(variable_mapper_);
        inprocess_scheduler_.attach_proof_manager(proof_manager_);
    }

    void solver_core::reset_episode_counters() noexcept
    {
        propagation_assignment_count_ = 0u;
        watch_entry_scan_count_ = 0u;
        binary_watch_scan_count_ = 0u;
        binary_watch_conflict_count_ = 0u;
        learned_clause_shrink_event_count_ = 0u;
        learned_clause_glue_total_ = 0u;
        learned_clause_glue_sample_count_ = 0u;
        minimized_clause_count_ = 0u;
        shrunk_clause_count_ = 0u;
        promoted_clause_count_ = 0u;
    }

    [[nodiscard]] bool solver_core::normalize_clause(const std::span<const literal> literals) noexcept
    {
        normalized_clause_scratch_.clear();
        if (++literal_stamp_ == 0u)
        {
            std::fill(literal_stamps_.begin(), literal_stamps_.end(), 0u);
            literal_stamp_ = 1u;
        }
        for (const auto lit: literals)
        {
            const auto slot = static_cast<std::size_t>(lit.raw());
            if (slot + 1u >= literal_stamps_.size())
                literal_stamps_.resize(slot + 2u, 0u);
            if (literal_stamps_[slot ^ 1u] == literal_stamp_)
                return false;
            if (literal_stamps_[slot] == literal_stamp_)
                continue;
            literal_stamps_[slot] = literal_stamp_;
            normalized_clause_scratch_.push_back(lit);
        }
        return true;
    }

    [[nodiscard]] std::uint32_t solver_core::find_max_variable(const std::span<const literal> assumptions) noexcept
    {
        // Seeded from what the problem was stated over, not from what survives simplification. Bounded
        // variable elimination removes a variable's clauses outright, so scanning only the current database
        // would shrink the variable range after preprocessing and the reported model would omit variables.
        // The same scan sizes the first phase's budget by the literals it will propagate over.
        std::uint32_t max_variable = max_problem_variable_;
        std::size_t literal_count {};
        const auto& storage = clause_database_.storage_of();
        const auto process_clause = [&storage, &max_variable, &literal_count](const clause::ref_t ref) noexcept
        {
            const auto literals = storage.view_literals(ref);
            literal_count += literals.size();
            for (const auto lit: literals)
                if (lit.variable_of().index() > max_variable)
                    max_variable = lit.variable_of().index();
        };
        clause_database_.iterate_irredundant(process_clause);
        clause_database_.iterate_redundant(process_clause);
        database_literal_count_ = literal_count;
        for (const auto lit: assumptions)
            if (lit.variable_of().index() > max_variable)
                max_variable = lit.variable_of().index();
        return max_variable;
    }

    void solver_core::initialize_search_state(const solve_request& request) noexcept
    {
        const auto slots = static_cast<std::size_t>(variable_count_) + 1u;
        values_.assign(slots * 2u, 0);
        levels_.assign(slots, 0u);
        reasons_.assign(slots, clause::ref_t {});
        trail_positions_.assign(slots, 0u);
        flags_.assign(slots, 0u);
        level_stamps_.assign(slots + 1u, 0u);
        chain_stamps_.assign(slots, 0u);
        trail_.clear();
        trail_.reserve(slots);
        control_.clear();
        control_.reserve(slots + 1u);
        control_.push_back(control_frame {});
        level_ = 0u;
        propagated_ = 0u;
        assumption_count_ = static_cast<std::uint32_t>(request.assumptions.size());
        analyzed_.clear();
        minimized_.clear();
        levels_touched_.clear();
        glue_stamp_ = 0u;
        chain_stamp_ = 0u;

        // Saved phases persist across episodes, so a later solve resumes near the assignment the previous
        // one settled on; only variables new to this episode get the default polarity.
        if (saved_phase_.size() < slots)
            saved_phase_.resize(slots, std::int8_t {1});

        heap_.resize(variable_count_);
        heap_.clear();
        for (std::uint32_t index = 1u; index <= variable_count_; ++index)
            heap_.push(index);
        local_search_enabled_ = false;
        opening_walk_pending_ = false;
        // Focused first: frequent restarts while the search has no history worth keeping, then the
        // configured (stable) schedule, the periods doubling so both modes get equal time in the long run.
        focused_mode_ = mode_switching_enabled && mode_start_focused;
        mode_period_ = mode_initial_period;
        mode_switch_at_ = mode_switching_enabled ? conflict_event_count() + mode_period_ : std::numeric_limits<counter_t>::max();
        search_coordinator_.set_restart_interval(focused_mode_ ? focused_restart_interval : stable_restart_interval_);
        next_local_search_at_ = local_search_initial_interval;
        visits_at_last_walk_ = watch_entry_scan_count_;
        best_walk_unsatisfied_ = std::numeric_limits<std::size_t>::max();
        local_search_scale_ = 1.0;
    }

    [[nodiscard]] bool solver_core::local_search_applicable(const std::uint32_t max_variable) const noexcept
    {
        if ((max_variable == 0u) || (max_variable > local_search_variable_limit))
            return false;
        const auto clause_count = clause_database_.stats_snapshot().irredundant_count;
        return (clause_count >= local_search_minimum_clause_count) && (clause_count <= local_search_clause_limit);
    }

    bool solver_core::walk_and_seed_phases(const std::size_t max_flips, const bool force) noexcept
    {
        // Once a strategy has produced the best assignment seen, later walks keep using it: alternating with
        // a rule that stalls on this formula would only waste every second walk.
        const auto config_index =
            (best_walk_config_ < local_search_portfolio.size()) ? best_walk_config_ : local_search_round_ % local_search_portfolio.size();
        const auto config = local_search_portfolio[config_index];
        ++local_search_round_;
        phase_scratch_.assign(static_cast<std::size_t>(variable_count_) + 1u, 0u);
        for (std::uint32_t index = 1u; index <= variable_count_; ++index)
            phase_scratch_[index] = (saved_phase_[index] > 0) ? 1u : 0u;
        const auto solved = local_search_.walk(config, max_flips, phase_scratch_);
        const auto unsatisfied = local_search_.best_unsatisfied_count();
        // Improvement means a near miss or a quarter fewer unsatisfied clauses: creeping down by one clause
        // per walk on a formula the walk cannot solve kept doubling the re-walk budget and reseeding the
        // phases of a search that was doing fine on its own.
        const bool improved = (unsatisfied < best_walk_unsatisfied_) &&
                              (unsatisfied <= local_search_near_miss_limit || unsatisfied * 4u <= best_walk_unsatisfied_ * 3u);
        if (!force && !solved && !improved)
            return false;
        best_walk_unsatisfied_ = unsatisfied;
        best_walk_config_ = config_index;
        const auto assignment = local_search_.best_assignment();
        for (std::uint32_t index = 1u; (index <= variable_count_) && (index < assignment.size()); ++index)
            saved_phase_[index] = (assignment[index] != 0u) ? std::int8_t {1} : std::int8_t {-1};
        complete_introduced_phases();
        return true;
    }

    void solver_core::complete_introduced_phases() noexcept
    {
        const auto witnesses = extension_stack_.witness_literals();
        for (const auto& record: extension_stack_.records())
        {
            const auto* factor = std::get_if<factor_transformation>(&record.payload);
            if (factor == nullptr)
                continue;
            const auto introduced = factor->introduced_variable.index();
            if ((introduced == 0u) || (introduced > variable_count_))
                continue;
            bool all_true = true;
            for (auto position = factor->witness_begin; (position < factor->witness_end) && (position < witnesses.size()); ++position)
            {
                const auto lit = witnesses[position];
                if (lit.raw() == 0u)
                    break;
                const auto phase = saved_phase_[lit.variable_of().index()];
                if (lit.is_negated() ? phase >= 0 : phase <= 0)
                {
                    all_true = false;
                    break;
                }
            }
            saved_phase_[introduced] = all_true ? std::int8_t {1} : std::int8_t {-1};
        }
    }

    bool solver_core::walk_in_rounds(const std::size_t budget, const bool force) noexcept
    {
        const auto rounds = local_search_opening_rounds;
        const auto round_flips = std::max<std::size_t>(1u, budget / rounds);
        const auto config_index = local_search_round_ % local_search_portfolio.size();
        std::size_t stalled {};
        for (std::size_t round = 0u; round < rounds; ++round)
        {
            const auto before = best_walk_unsatisfied_;
            local_search_round_ = config_index;
            if (walk_and_seed_phases(round_flips, force && (round == 0u)) && (best_walk_unsatisfied_ == 0u))
                return true;
            stalled = (best_walk_unsatisfied_ < before) ? 0u : stalled + 1u;
            if (stalled >= 2u)
                break;
            // Walk-friendly formulas are within two unsatisfied clauses after a few rounds (`lran_f2000`
            // after one, `gcp125_17` from the start); the ones that creep from seven to one over ten rounds
            // never finish, and on a formula the search solves in a few thousand conflicts those rounds were
            // most of the run (`hanoi4`: 79% of its instructions). Past the third round only a near miss
            // earns the rest of the budget.
            if ((round >= 1u) && (best_walk_unsatisfied_ > local_search_near_miss_limit))
                break;
        }
        local_search_round_ = config_index + 1u;
        return false;
    }

    void solver_core::probe_failed_literals() noexcept
    {
        // A probed unit is a RUP step without antecedents; the LRAT consumers need chains, so with a proof
        // consumer attached the search derives its units with the analysis that produces them.
        if ((variable_count_ == 0u) || (level_ != 0u) || proof_manager_.has_active_consumers())
            return;
        if (!assign_root_units())
            return;
        if (propagate().valid())
        {
            root_refuted_ = true;
            return;
        }
        const auto has_binary = [this](const literal lit) noexcept
        {
            for (const auto& entry: watch_list_.list_at(lit.raw()))
                if (entry.is_binary())
                    return true;
            return false;
        };
        // The first round is cheap; each round after a productive one gets four times the budget, so a formula
        // where probing yields nothing (random 3-SAT, the scheduling instances) pays a few thousand
        // assignments and one where it pays (`bmc-ibm-13`: 131 units) gets the full pass.
        const auto base_effort =
            static_cast<std::size_t>(clause_database_.stats_snapshot().irredundant_count) * probe_effort_per_clause + probe_minimum_effort;
        std::size_t effort = base_effort;
        probe_marks_.assign(static_cast<std::size_t>(variable_count_) * 2u + 2u, 0u);
        const auto clear_probe_marks = [this]() noexcept
        {
            for (const auto marked: marked_scratch_)
                probe_marks_[marked.raw()] = 0u;
            marked_scratch_.clear();
        };
        const auto learn_root_unit = [this](const literal unit) noexcept
        {
            const std::array<literal, 1u> unit_clause {unit};
            const auto ref = clause_database_.add_clause(unit_clause, true);
            if (!ref.valid())
                return false;
            unit_clause_refs_.push_back(ref);
            if (proof_manager_.has_active_consumers())
                proof_manager_.on_add_derived(ref, unit_clause);
            // A probed unit is a learned clause in every sense, including the episode's retention accounting.
            search_coordinator_.note_learned_clause();
            incremental_context_.retain_learned_clause();
            assign(unit, ref);
            ++probe_unit_count_;
            if (propagate().valid())
            {
                root_refuted_ = true;
                return false;
            }
            return true;
        };
        // Rounds: a unit derived in one round falsifies further probes in the next; three rounds cover what
        // the effort bound leaves.
        for (std::size_t round = 0u; round < probe_rounds; ++round)
        {
            if (round != 0u)
                effort = base_effort * probe_round_growth;
            const auto units_before = probe_unit_count_ + probe_lifted_count_;
            for (std::uint32_t var = 1u; (var <= variable_count_) && (effort != 0u); ++var)
            {
                if (is_assigned(var))
                    continue;
                const literal positive {variable {var}, false};
                if (!has_binary(positive) && !has_binary(positive.negated()))
                    continue;
                // First polarity: remember what it implies for lifting against the second.
                lifted_scratch_.clear();
                clear_probe_marks();
                bool positive_failed {};
                {
                    ++probe_count_;
                    const auto trail_before = trail_.size();
                    new_level(positive);
                    assign(positive, clause::ref_t {});
                    const auto conflict = propagate();
                    const auto grown = trail_.size() - trail_before;
                    effort = (grown >= effort) ? 0u : effort - grown;
                    if (conflict.valid())
                        positive_failed = true;
                    else
                        for (auto index = trail_before + 1u; index < trail_.size(); ++index)
                        {
                            probe_marks_[trail_[index].raw()] = 1u;
                            marked_scratch_.push_back(trail_[index]);
                        }
                    backtrack(0u);
                }
                if (positive_failed)
                {
                    clear_probe_marks();
                    if (!learn_root_unit(positive.negated()))
                        return;
                    continue;
                }
                if (effort == 0u)
                {
                    // Leaving with the positive probe's marks set would let a later variable's negative probe
                    // lift a literal only this probe implied: a wrong unit, and a wrong UNSAT verdict on
                    // `bmc-ibm-1/12/13` before this was caught.
                    clear_probe_marks();
                    break;
                }
                // Second polarity: a conflict makes the first a unit; otherwise the intersection of the two
                // implied sets lifts to units.
                {
                    ++probe_count_;
                    const literal negative = positive.negated();
                    const auto trail_before = trail_.size();
                    new_level(negative);
                    assign(negative, clause::ref_t {});
                    const auto conflict = propagate();
                    const auto grown = trail_.size() - trail_before;
                    effort = (grown >= effort) ? 0u : effort - grown;
                    if (!conflict.valid())
                        for (auto index = trail_before + 1u; index < trail_.size(); ++index)
                            if (probe_marks_[trail_[index].raw()] != 0u)
                                lifted_scratch_.push_back(trail_[index]);
                    backtrack(0u);
                    clear_probe_marks();
                    if (conflict.valid())
                    {
                        if (!learn_root_unit(positive))
                            return;
                        continue;
                    }
                }
                for (const auto lifted: lifted_scratch_)
                {
                    if (is_assigned(var_of(lifted)))
                        continue;
                    ++probe_lifted_count_;
                    if (!learn_root_unit(lifted))
                        return;
                }
            }
            if (probe_unit_count_ + probe_lifted_count_ == units_before)
                break;
        }
    }

    [[nodiscard]] bool solver_core::try_lucky_assignments(const solve_request& request, const bool count_decisions) noexcept
    {
        if (!request.assumptions.empty() || (variable_count_ == 0u) || (level_ != 0u) || root_refuted_)
            return false;
        // Unit clauses live outside the watch lists; without them assigned first an attempt can satisfy
        // every watched clause and still contradict a unit, which is what the model verifier caught.
        if (!assign_root_units())
            return false;
        if (propagate().valid())
        {
            root_refuted_ = true;
            return false;
        }
        const auto root_trail = trail_.size();
        for (std::uint32_t strategy = 0u; strategy < 4u; ++strategy)
        {
            const bool negative = (strategy & 1u) != 0u;
            const bool backward = strategy >= 2u;
            bool failed {};
            for (std::uint32_t step = 1u; (step <= variable_count_) && !failed; ++step)
            {
                const auto var = backward ? variable_count_ + 1u - step : step;
                if (is_assigned(var))
                    continue;
                // Each attempt's decisions count against the request's decision limit like any other, so a
                // limited request that would have stopped in the search stops here too. The raw-formula phase
                // runs without limits and leaves the coordinator untouched, so its attempts are not counted.
                if (count_decisions)
                    search_coordinator_.note_decision();
                if (count_decisions && (search_coordinator_.current_outcome() == search_coordinator::outcome::terminated))
                {
                    backtrack(0u);
                    if (!opening_phase_snapshot_.empty())
                        std::copy(opening_phase_snapshot_.begin(), opening_phase_snapshot_.end(), saved_phase_.begin());
                    return false;
                }
                const literal decision {variable {var}, negative};
                new_level(decision);
                assign(decision, clause::ref_t {});
                failed = propagate().valid();
            }
            if (!failed && (trail_.size() == static_cast<std::size_t>(variable_count_)))
            {
                ++lucky_success_count_;
                return true;
            }
            backtrack(0u);
            propagated_ = root_trail;
        }
        if (!opening_phase_snapshot_.empty())
            std::copy(opening_phase_snapshot_.begin(), opening_phase_snapshot_.end(), saved_phase_.begin());
        return false;
    }

    [[nodiscard]] bool solver_core::prepare_opening_walk() noexcept
    {
        local_search_round_ = 0u;
        best_walk_config_ = local_search_portfolio.size();
        local_search_enabled_ = local_search_prepared_;
        if (!local_search_enabled_)
            return false;
        if (local_search_.maximum_clause_size() > 3u)
            local_search_round_ = 1u;

        local_search_.set_seed(local_search_seed);
        const auto budget = static_cast<std::size_t>(local_search_variable_count_) * local_search_flips_per_variable_;
        if ((budget == 0u) || (budget > local_search_opening_flip_cap))
            return false;
        // The walk starts from the phase snapshot `solve` took before probing, not from what the probe
        // leaves behind: the probe's phase-saved assignment is a local minimum the walk climbs out of
        // slowly, and starting there turned a 0.2 s solve of `lran_f2000` into a timeout.
        return true;
    }

    [[nodiscard]] bool solver_core::run_opening_walk() noexcept
    {
        // The probe's phase-saved assignment is kept aside: a walk that ends far from a model has nothing
        // better to offer than what the search already learned (`ais12`: 12,004 conflicts after adopting
        // such a walk, 7,580 without), so only a near miss replaces it.
        probe_phase_scratch_.assign(saved_phase_.begin(), saved_phase_.end());
        std::copy(opening_phase_snapshot_.begin(), opening_phase_snapshot_.end(), saved_phase_.begin());
        const auto budget = static_cast<std::size_t>(local_search_variable_count_) * local_search_flips_per_variable_;
        if (walk_in_rounds(budget / 2u, true))
            return true;
        // The second rule gets its turn only after a near miss: every formula the opening walk has solved fell
        // to the first rule, and on the ones it cannot solve the second rule doubled the cost (`hanoi4`,
        // `ais12`: 50-60 ms of walking against a search that finishes in under 10 ms).
        if (best_walk_unsatisfied_ > local_search_near_miss_limit)
        {
            std::copy(probe_phase_scratch_.begin(), probe_phase_scratch_.end(), saved_phase_.begin());
            return false;
        }
        const auto first_best = best_walk_config_;
        best_walk_config_ = local_search_portfolio.size();
        const auto second_best_before = best_walk_unsatisfied_;
        const auto solved = walk_in_rounds(budget / 2u, false);
        if (!solved && (best_walk_unsatisfied_ >= second_best_before))
            best_walk_config_ = first_best;
        if (!solved && (best_walk_unsatisfied_ > local_search_near_miss_limit))
            std::copy(probe_phase_scratch_.begin(), probe_phase_scratch_.end(), saved_phase_.begin());
        return solved;
    }

    void solver_core::rewalk_if_due() noexcept
    {
        if (!local_search_enabled_ || (local_search_effort_percent_ == 0u) || opening_walk_pending_)
            return;
        const auto conflicts = search_coordinator_.conflict_event_count();
        if (conflicts < next_local_search_at_)
            return;
        const auto visits = watch_entry_scan_count_ - visits_at_last_walk_;
        visits_at_last_walk_ = watch_entry_scan_count_;
        const auto interval = (next_local_search_at_ == 0u) ? local_search_initial_interval : next_local_search_at_ * 2u;
        next_local_search_at_ = conflicts + interval;
        const auto minimum = std::min(static_cast<std::size_t>(local_search_variable_count_) * local_search_rewalk_floor_per_variable,
                                      local_search_rewalk_floor_cap);
        const auto share = static_cast<double>(visits) * static_cast<double>(local_search_effort_percent_) / (100.0 * visits_per_flip);
        const auto budget = std::max(minimum, static_cast<std::size_t>(share * local_search_scale_));
        const auto improved = walk_and_seed_phases(budget, false);
        local_search_scale_ = improved ? std::min(local_search_scale_ * 2.0, local_search_scale_ceiling) :
                                         std::max(local_search_scale_ * 0.5, local_search_scale_floor);
    }

    void solver_core::rebuild_propagation_state() noexcept
    {
        watch_list_.reserve(static_cast<std::size_t>(variable_count_) * 2u + 2u);
        watch_list_.clear_entries();
        unit_clause_refs_.clear();
        // Each list is sized before it is filled: grown one push at a time, the lists of a 55k-clause
        // formula went through tens of thousands of reallocations for a state that is rebuilt three times.
        const auto& storage = clause_database_.storage_of();
        watch_count_scratch_.assign(static_cast<std::size_t>(variable_count_) * 2u + 2u, 0u);
        const auto count = [this, &storage](const clause::ref_t ref) noexcept
        {
            const auto literals = storage.view_literals(ref);
            if (literals.size() < 2u)
                return;
            for (const auto lit: {literals[0u], literals[1u]})
            {
                const auto index = static_cast<std::size_t>(lit.index_in_watch_bank());
                if (index < watch_count_scratch_.size())
                    ++watch_count_scratch_[index];
            }
        };
        clause_database_.iterate_irredundant(count);
        clause_database_.iterate_redundant(count);
        // An eighth of slack on top of the exact count: sized exactly, every list reallocated on the first
        // learned clause that watched it (a thousand reallocations on `dubois100`).
        for (std::size_t index = 0u; index < watch_count_scratch_.size(); ++index)
            if (watch_count_scratch_[index] != 0u)
                watch_list_.list_at(index).reserve(watch_count_scratch_[index] + watch_count_scratch_[index] / 8u + 2u);
        const auto attach = [this](const clause::ref_t ref) noexcept { attach_clause_for_propagation(ref); };
        clause_database_.iterate_irredundant(attach);
        clause_database_.iterate_redundant(attach);
    }

    void solver_core::attach_clause_for_propagation(const clause::ref_t ref) noexcept
    {
        const auto& storage = clause_database_.storage_of();
        const auto literals = storage.view_literals(ref);
        if (literals.size() <= 1u)
        {
            unit_clause_refs_.push_back(ref);
            return;
        }
        const auto is_binary = literals.size() == 2u;
        watch_list_.push_watch(literals[0u], watch {literals[1u], ref, is_binary});
        watch_list_.push_watch(literals[1u], watch {literals[0u], ref, is_binary});
    }

    [[nodiscard]] bool solver_core::assign_root_units() noexcept
    {
        const auto& storage = clause_database_.storage_of();
        for (const auto ref: unit_clause_refs_)
        {
            if (!storage.is_alive(ref))
                continue;
            const auto literals = storage.view_literals(ref);
            if (literals.empty())
                return false;
            const auto lit = literals.front();
            const auto value = value_of(lit);
            if (value < 0)
                return false;
            if (value == 0)
            {
                assign(lit, ref);
                ++propagation_assignment_count_;
            }
        }
        return true;
    }

    void solver_core::backtrack(const std::uint32_t target_level) noexcept
    {
        if (target_level >= level_)
            return;
        const auto begin = control_[target_level + 1u].trail_begin;
        for (auto index = trail_.size(); index-- > begin;)
        {
            const auto lit = trail_[index];
            const auto var = var_of(lit);
            values_[lit.raw()] = 0;
            values_[lit.raw() ^ 1u] = 0;
            if (!heap_.contains(var))
                heap_.push(var);
        }
        trail_.resize(begin);
        if (propagated_ > begin)
            propagated_ = begin;
        control_.resize(target_level + 1u);
        level_ = target_level;
    }

    [[nodiscard]] clause::ref_t solver_core::propagate() noexcept
    {
        (void)propagator_.propagate();
        auto& storage = clause_database_.storage_of();
        std::int8_t* const values = values_.data();
        // The counters are settled per partition and at the end, not per entry: every value live inside the
        // scan loop competes for a register with the cursors and the value table, and a counter that lost
        // that competition put the table on the stack in one build.
        const auto trail_before = trail_.size();
        std::size_t visits {};
        std::size_t binary_visits {};
        std::size_t partitions {};
        clause::ref_t conflict {};

        while (propagated_ < trail_.size())
        {
            const literal lit = trail_[propagated_++];
            const literal not_lit = lit.negated();
            auto& list = watch_list_.list_at(not_lit.raw());
            ++partitions;
            watch* const begin = list.data();
            watch* const end = begin + list.size();
            watch* read = begin;
            watch* write = begin;

            while (read != end)
            {
                const watch entry = *read++;
                const literal blocking = entry.blocking_literal();
                const auto blocking_value = values[blocking.raw()];
                if (blocking_value > 0)
                {
                    *write++ = entry;
                    continue;
                }

                if (entry.is_binary())
                {
                    ++binary_visits;
                    *write++ = entry;
                    if (blocking_value < 0)
                    {
                        ++binary_watch_conflict_count_;
                        conflict = entry.clause_ref();
                        break;
                    }
                    assign(blocking, entry.clause_ref());
                    continue;
                }

                // Resolved once: the two accessors below would otherwise each test for relocations.
                const clause::ref_t ref = storage.resolve_ref(clause::ref_t {entry.raw_offset()});
                literal* const lits = storage.literal_data_at_home(ref);
                const literal other {lits[0].raw() ^ lits[1].raw() ^ not_lit.raw()};
                const auto other_value = values[other.raw()];
                if (other_value > 0)
                {
                    *write++ = watch {other, ref, false};
                    continue;
                }

                literal* const lits_end = lits + storage.header_at_home(ref).size;
                literal* candidate = lits + 2;
                while ((candidate != lits_end) && (values[candidate->raw()] < 0))
                    ++candidate;

                if (candidate != lits_end)
                {
                    const literal replacement = *candidate;
                    lits[0] = other;
                    lits[1] = replacement;
                    *candidate = not_lit;
                    watch_list_.push_watch_at(replacement.raw(), watch {other, ref, false});
                    continue;
                }

                *write++ = watch {other, ref, false};
                if (other_value < 0)
                {
                    conflict = ref;
                    break;
                }
                lits[0] = other;
                lits[1] = not_lit;
                assign(other, ref);
            }

            visits += static_cast<std::size_t>(read - begin);
            while (read != end)
                *write++ = *read++;
            list.resize(static_cast<std::size_t>(write - begin));
            if (conflict.valid())
                break;
        }

        watch_entry_scan_count_ += visits;
        binary_watch_scan_count_ += binary_visits;
        propagation_assignment_count_ += trail_.size() - trail_before;
        watch_list_.add_iterate_count(partitions);
        return conflict;
    }

    void solver_core::bump_clause(const clause::ref_t ref) noexcept
    {
        auto& header = clause_database_.storage_of().header_at(ref);
        // Two counts per use: the reduction pass treats two or more as "used since the last pass" and leaves
        // one behind as a pass of grace for mid-glue clauses, which it then clears.
        header.used = static_cast<std::uint8_t>(std::min<unsigned>(255u, static_cast<unsigned>(header.used) + 2u));
        header.activity += 1.0f;
    }

    void solver_core::collect_root_chain(const std::uint32_t var) noexcept
    {
        if (chain_stamps_[var] == chain_stamp_)
            return;
        chain_stamps_[var] = chain_stamp_;
        const auto reason = reasons_[var];
        if (!reason.valid())
            return;
        const auto& storage = clause_database_.storage_of();
        for (const auto other: storage.view_literals(reason))
            if (var_of(other) != var)
                collect_root_chain(var_of(other));
        root_chain_refs_.push_back(reason);
    }

    void solver_core::collect_minimize_chain(const std::uint32_t var) noexcept
    {
        if (chain_stamps_[var] == chain_stamp_)
            return;
        chain_stamps_[var] = chain_stamp_;
        const auto reason = reasons_[var];
        const auto& storage = clause_database_.storage_of();
        for (const auto other: storage.view_literals(reason))
        {
            const auto other_var = var_of(other);
            if (other_var == var)
                continue;
            if (levels_[other_var] == 0u)
                collect_root_chain(other_var);
            else if (((flags_[other_var] & keep_flag) == 0u) && ((flags_[other_var] & removable_flag) != 0u))
                collect_minimize_chain(other_var);
        }
        minimize_chain_refs_.push_back(reason);
    }

    [[nodiscard]] bool solver_core::minimize_literal(const std::uint32_t var) noexcept
    {
        // The checks a visit makes before it is worth expanding; `depth` is the stack depth of the visit.
        const auto verdict = [this](const std::uint32_t candidate, const std::uint32_t depth) noexcept -> std::int8_t
        {
            const auto candidate_flags = flags_[candidate];
            const auto lvl = levels_[candidate];
            if ((lvl == 0u) || ((candidate_flags & (removable_flag | keep_flag)) != 0u))
                return 1;
            if (!reasons_[candidate].valid() || ((candidate_flags & poison_flag) != 0u) || (lvl == level_))
                return -1;
            const auto& frame = control_[lvl];
            if ((depth == 0u) && (frame.seen_count < 2u))
                return -1;
            if (trail_positions_[candidate] <= frame.seen_min_trail)
                return -1;
            if (depth > minimize_depth_limit)
                return -1;
            return 0;
        };
        const auto& storage = clause_database_.storage_of();
        const auto open = [&storage, this](const std::uint32_t candidate) noexcept
        {
            const auto reason = reasons_[candidate];
            const literal* const lits = storage.literal_data(reason);
            minimize_stack_.push_back(minimize_frame {candidate, lits, lits + storage.header_at(reason).size});
        };

        const auto top_verdict = verdict(var, 0u);
        if (top_verdict != 0)
            return top_verdict > 0;
        minimize_stack_.clear();
        open(var);
        for (;;)
        {
            auto& frame = minimize_stack_.back();
            if (frame.cursor == frame.end)
            {
                // Every reason literal was removable: so is this one.
                flags_[frame.var] |= removable_flag;
                minimized_.push_back(frame.var);
                minimize_stack_.pop_back();
                if (minimize_stack_.empty())
                    return true;
                continue;
            }
            const auto other_var = var_of(*frame.cursor++);
            if (other_var == frame.var)
                continue;
            const auto other_verdict = verdict(other_var, static_cast<std::uint32_t>(minimize_stack_.size()));
            if (other_verdict > 0)
                continue;
            if (other_verdict < 0)
            {
                // A literal that cannot be removed poisons the whole path, innermost first.
                for (auto it = minimize_stack_.rbegin(); it != minimize_stack_.rend(); ++it)
                {
                    flags_[it->var] |= poison_flag;
                    minimized_.push_back(it->var);
                }
                minimize_stack_.clear();
                return false;
            }
            open(other_var);
        }
    }

    [[nodiscard]] std::uint32_t solver_core::analyze_conflict(const clause::ref_t conflict) noexcept
    {
        auto& storage = clause_database_.storage_of();
        const bool want_chain = proof_manager_.has_active_consumers();
        learned_.clear();
        learned_.push_back(literal {});
        analyzed_.clear();
        minimized_.clear();
        levels_touched_.clear();
        resolution_chain_refs_.clear();
        root_chain_refs_.clear();
        minimize_chain_refs_.clear();
        if (want_chain && (++chain_stamp_ == 0u))
        {
            std::fill(chain_stamps_.begin(), chain_stamps_.end(), 0u);
            chain_stamp_ = 1u;
        }

        std::uint32_t open {};
        auto trail_index = trail_.size();
        literal uip {};
        clause::ref_t reason = conflict;
        for (;;)
        {
            bump_clause(reason);
            if (want_chain)
                resolution_chain_refs_.push_back(reason);
            const literal* const lits = storage.literal_data(reason);
            const literal* const lits_end = lits + storage.header_at(reason).size;
            for (const literal* current = lits; current != lits_end; ++current)
                if (*current != uip)
                    analyze_literal(*current, open, want_chain);

            for (;;)
            {
                const literal candidate = trail_[--trail_index];
                const auto var = var_of(candidate);
                if (((flags_[var] & seen_flag) != 0u) && (levels_[var] == level_))
                {
                    uip = candidate;
                    break;
                }
            }
            if (--open == 0u)
                break;
            reason = reasons_[var_of(uip)];
        }
        learned_[0u] = uip.negated();

        // Minimize: literals earlier on the trail are decided first, so that their keep/removable marks are
        // available when later, deeper literals are examined. A unit has nothing to minimize.
        if (learned_.size() > 1u)
            ++minimized_clause_count_;
        if (learned_.size() > 2u)
            std::sort(learned_.begin() + 1L, learned_.end(), [this](const literal left, const literal right) noexcept
                      { return trail_positions_[var_of(left)] < trail_positions_[var_of(right)]; });
        std::size_t write {1u};
        for (std::size_t index = 1u; index < learned_.size(); ++index)
        {
            const auto lit = learned_[index];
            const auto var = var_of(lit);
            if (minimize_literal(var))
            {
                if (want_chain)
                    collect_minimize_chain(var);
                continue;
            }
            flags_[var] |= keep_flag;
            learned_[write++] = lit;
        }
        if (write < learned_.size())
        {
            ++shrunk_clause_count_;
            learned_.resize(write);
        }

        // The literal of highest level after the UIP goes second: it is the backjump level's watch partner.
        std::uint32_t glue {1u};
        if (learned_.size() > 1u)
        {
            std::size_t best_index {1u};
            auto best_level = levels_[var_of(learned_[1u])];
            ++glue_stamp_;
            if (glue_stamp_ == 0u)
            {
                std::fill(level_stamps_.begin(), level_stamps_.end(), 0u);
                glue_stamp_ = 1u;
            }
            level_stamps_[level_] = glue_stamp_;
            for (std::size_t index = 1u; index < learned_.size(); ++index)
            {
                const auto lvl = levels_[var_of(learned_[index])];
                if (level_stamps_[lvl] != glue_stamp_)
                {
                    level_stamps_[lvl] = glue_stamp_;
                    ++glue;
                }
                if (lvl > best_level)
                {
                    best_level = lvl;
                    best_index = index;
                }
            }
            if (best_index != 1u)
                std::swap(learned_[1u], learned_[best_index]);
        }

        // Reason-side bumping: the variables that forced the learned clause's literals are as much a part of
        // the conflict as the literals themselves; bumping them focuses the search on the structure behind
        // the clause rather than only its surface. Kissat and CaDiCaL both do this for short clauses.
        if (learned_.size() <= reason_bump_size_limit)
        {
            for (std::size_t index = 1u; index < learned_.size(); ++index)
            {
                const auto reason_ref = reasons_[var_of(learned_[index])];
                if (!reason_ref.valid())
                    continue;
                const literal* const lits = storage.literal_data(reason_ref);
                const literal* const lits_end = lits + storage.header_at(reason_ref).size;
                for (const literal* current = lits; current != lits_end; ++current)
                {
                    const auto var = var_of(*current);
                    if (((flags_[var] & seen_flag) != 0u) || (levels_[var] == 0u))
                        continue;
                    flags_[var] |= seen_flag;
                    analyzed_.push_back(var);
                }
            }
        }

        for (const auto var: analyzed_)
            heap_.bump(var);
        heap_.decay();

        for (const auto var: analyzed_)
            flags_[var] = 0u;
        for (const auto var: minimized_)
            flags_[var] = 0u;
        for (const auto lvl: levels_touched_)
            control_[lvl].seen_count = 0u;
        return glue;
    }

    void solver_core::learn_clause(const std::uint32_t glue) noexcept
    {
        const auto backjump_level = (learned_.size() > 1u) ? levels_[var_of(learned_[1u])] : 0u;
        backtrack(backjump_level);

        const auto ref = clause_database_.add_clause(learned_, true);
        auto& header = clause_database_.storage_of().header_at(ref);
        header.glue = glue;
        header.tier = static_cast<std::uint8_t>((glue <= core_glue_limit) ? 0u : (glue <= retained_glue_limit) ? 1u : 2u);
        if (header.tier == 0u)
            ++promoted_clause_count_;

        if (learned_.size() == 1u)
            unit_clause_refs_.push_back(ref);
        else
            attach_clause_for_propagation(ref);
        assign(learned_[0u], ref);

        if (proof_manager_.has_active_consumers())
        {
            antecedents_scratch_.clear();
            const auto append = [this](const clause::ref_t chain_ref) noexcept
            {
                const auto id = proof_manager_.stable_id_for_clause(chain_ref);
                if (id.valid())
                    antecedents_scratch_.push_back(id);
            };
            for (const auto chain_ref: root_chain_refs_)
                append(chain_ref);
            for (const auto chain_ref: minimize_chain_refs_)
                append(chain_ref);
            for (auto it = resolution_chain_refs_.rbegin(); it != resolution_chain_refs_.rend(); ++it)
                append(*it);
            proof_manager_.on_add_derived(ref, learned_, antecedents_scratch_);
        }
        else
            proof_manager_.on_add_derived(ref, learned_);

        learned_clause_glue_total_ += glue;
        ++learned_clause_glue_sample_count_;
        search_coordinator_.note_learned_clause();
        incremental_context_.retain_learned_clause();
    }

    void solver_core::analyze_final(const literal failed) noexcept
    {
        failed_core_.clear();
        failed_core_.push_back(failed);
        analyzed_.clear();
        const auto failed_var = var_of(failed);
        flags_[failed_var] |= seen_flag;
        analyzed_.push_back(failed_var);

        const auto& storage = clause_database_.storage_of();
        for (auto index = trail_.size(); index-- > 0u;)
        {
            const auto lit = trail_[index];
            const auto var = var_of(lit);
            if (((flags_[var] & seen_flag) == 0u) || (levels_[var] == 0u))
                continue;
            const auto reason = reasons_[var];
            if (!reason.valid())
            {
                // A decision below the search levels is an assumption; the failed literal's own negation
                // reaches here only when the caller assumed both polarities.
                failed_core_.push_back(lit);
                continue;
            }
            for (const auto other: storage.view_literals(reason))
            {
                const auto other_var = var_of(other);
                if ((flags_[other_var] & seen_flag) == 0u)
                {
                    flags_[other_var] |= seen_flag;
                    analyzed_.push_back(other_var);
                }
            }
        }
        for (const auto var: analyzed_)
            flags_[var] = 0u;
        analyzed_.clear();
    }

    void solver_core::restart() noexcept
    {
        std::uint32_t reuse = assumption_count_;
        if (reuse < level_)
        {
            while (!heap_.empty() && is_assigned(heap_.top()))
                (void)heap_.pop();
            if (!heap_.empty())
            {
                const auto next_score = heap_.score(heap_.top());
                while (reuse < level_)
                {
                    const auto decision = control_[reuse + 1u].decision;
                    if ((decision.raw() == 0u) || (heap_.score(var_of(decision)) < next_score))
                        break;
                    ++reuse;
                }
            }
        }
        backtrack(reuse);
        search_coordinator_.note_restart();
    }

    void solver_core::protect_trail_reasons(const bool protect) noexcept
    {
        for (const auto lit: trail_)
        {
            const auto reason = reasons_[var_of(lit)];
            if (!reason.valid())
                continue;
            if (protect)
                clause_database_.mark_reason_clause(reason);
            else
                clause_database_.unmark_reason_clause(reason);
        }
    }

    void solver_core::reduce() noexcept
    {
        protect_trail_reasons(true);
        auto& controller = search_coordinator_.reduce_controller();
        controller.select_reduction_candidates(clause_database_);
        controller.reduce_clauses(clause_database_);
        controller.flush_redundant(clause_database_);
        controller.update_tiers(clause_database_);
        clause_database_.decay_quality();
        protect_trail_reasons(false);
        collect_garbage();
    }

    void solver_core::collect_garbage() noexcept
    {
        const auto slots = clause_database_.storage_of().arena_bytes() / sizeof(std::uint32_t) + 1u;
        forwarding_.assign(slots, clause::ref_t::invalid_offset);
        clause_database_.compact([this](const clause::ref_t old_ref, const clause::ref_t new_ref) noexcept
                                 { forwarding_[old_ref.offset() / sizeof(std::uint32_t)] = new_ref.offset(); });
        const auto forward = [this](const clause::ref_t ref) noexcept
        { return clause::ref_t {forwarding_[ref.offset() / sizeof(std::uint32_t)]}; };
        watch_list_.rewrite_refs(forward);
        for (const auto lit: trail_)
        {
            auto& reason = reasons_[var_of(lit)];
            if (reason.valid())
                reason = forward(reason);
        }
        std::size_t write {};
        for (const auto ref: unit_clause_refs_)
        {
            const auto moved = forward(ref);
            if (moved.valid())
                unit_clause_refs_[write++] = moved;
        }
        unit_clause_refs_.resize(write);
        forwarding_.clear();
    }

    [[nodiscard]] solver_core::status solver_core::run_inprocess_if_due() noexcept
    {
        // The first phase's database goes back byte for byte afterwards; an inprocessing epoch would not.
        if (raw_phase_active_)
            return status::unknown;
        inprocess_scheduler_.set_conflicts_seen(search_coordinator_.conflict_event_count());
        inprocess_scheduler_.set_restart_count(search_coordinator_.restart_count());
        inprocess_scheduler_.set_decisions_seen(search_coordinator_.decision_event_count());
        inprocess_scheduler_.set_reduction_passes_seen(search_coordinator_.reduction_pass_count());
        inprocess_scheduler_.set_learned_clauses_seen(static_cast<counter_t>(search_coordinator_.learned_clause_count()));
        if (!inprocess_scheduler_.should_run())
            return status::unknown;

        backtrack(0u);
        protect_trail_reasons(true);
        const auto epochs_before = inprocess_scheduler_.epoch_count();
        inprocess_scheduler_.run_epoch();
        protect_trail_reasons(false);
        if (inprocess_scheduler_.epoch_count() == epochs_before)
            return status::unknown;

        inprocess_scheduler_.report_epoch_summary();
        search_coordinator_.notify_inprocess_epoch_completed(inprocess_scheduler_.last_structural_gain());
        rebuild_propagation_state();
        propagated_ = 0u;
        if (!assign_root_units())
            return (root_conflict() == status::unsatisfiable) ? status::unsatisfiable : status::unknown;
        return status::unknown;
    }

    void solver_core::note_conflict(const std::uint32_t glue) noexcept
    {
        search_coordinator_.note_conflict(glue);
        if ((evsids_maintenance_interval_ != 0u) && (search_coordinator_.conflict_event_count() % evsids_maintenance_interval_ == 0u))
            heap_.rescale_by(0.5);
    }

    [[nodiscard]] solver_core::status solver_core::root_conflict() noexcept
    {
        note_conflict(0u);
        if (search_coordinator_.current_outcome() == search_coordinator::outcome::terminated)
            return status::unknown;
        return status::unsatisfiable;
    }

    [[nodiscard]] solver_core::status solver_core::run_search(const solve_request& request) noexcept
    {
        if (!assign_root_units())
            return root_conflict();

        for (;;)
        {
            const auto conflict = propagate();
            if (conflict.valid())
            {
                if (level_ == 0u)
                    return root_conflict();
                const auto glue = analyze_conflict(conflict);
                learn_clause(glue);
                note_conflict(glue);
                if (search_coordinator_.current_outcome() == search_coordinator::outcome::terminated)
                    return status::unknown;
                if ((raw_probe_propagation_limit_ != 0u) && (propagation_assignment_count_ >= raw_probe_propagation_limit_))
                    return status::unknown;
                continue;
            }

            if (level_ < assumption_count_)
            {
                const auto assumption = request.assumptions[level_];
                const auto value = value_of(assumption);
                if (value < 0)
                {
                    // A falsified assumption is the episode's conflict: it is counted like one, so limits and
                    // statistics treat an assumption-refuted episode the same way as a root-refuted one.
                    analyze_final(assumption);
                    return root_conflict();
                }
                if (value > 0)
                {
                    new_level(literal {});
                    continue;
                }
                new_level(assumption);
                assign(assumption, clause::ref_t {});
                continue;
            }

            if (opening_walk_pending_ && (search_coordinator_.conflict_event_count() >= opening_walk_due_at_))
            {
                // The probe is over: a full restart, not the reusing kind, because the trail prefix a restart
                // keeps follows the phases the walk is about to replace.
                opening_walk_pending_ = false;
                backtrack(assumption_count_);
                search_coordinator_.note_restart();
                (void)run_opening_walk();
                continue;
            }

            if (conflict_event_count() >= mode_switch_at_)
            {
                // Stable/focused alternation, as CaDiCaL and Kissat do it: a satisfiable structured formula
                // (`hanoi5`: 30k conflicts at a 50-conflict interval, 109k at 4,096) wants frequent restarts
                // while random 3-SAT wants rare ones, and neither can be told apart in advance.
                focused_mode_ = !focused_mode_;
                if (focused_mode_)
                    mode_period_ *= 2u;
                // Stable periods are four times the focused ones: random 3-SAT wants the stable schedule for
                // most of the run, and the structured formulas need only a few focused periods.
                mode_switch_at_ = conflict_event_count() + (focused_mode_ ? mode_period_ : mode_period_ * stable_period_factor);
                search_coordinator_.set_restart_interval(focused_mode_ ? focused_restart_interval : stable_restart_interval_);
            }

            if (search_coordinator_.should_restart())
            {
                if (search_coordinator_.has_progress_since_restart())
                {
                    restart();
                    rewalk_if_due();
                    if (run_inprocess_if_due() == status::unsatisfiable)
                        return status::unsatisfiable;
                }
                else
                    search_coordinator_.note_restart();
                continue;
            }

            if (search_coordinator_.should_reduce())
                reduce();

            std::uint32_t decision_var {};
            while (!heap_.empty())
            {
                const auto candidate = heap_.pop();
                if (!is_assigned(candidate))
                {
                    decision_var = candidate;
                    break;
                }
            }
            if (decision_var == 0u)
                return status::satisfiable;

            const literal decision {variable {decision_var}, saved_phase_[decision_var] < 0};
            new_level(decision);
            assign(decision, clause::ref_t {});
            search_coordinator_.note_decision();
            if (search_coordinator_.current_outcome() == search_coordinator::outcome::terminated)
                return status::unknown;
        }
    }

    void solver_core::build_internal_model() noexcept
    {
        internal_model_.clear();
        internal_model_.reserve(variable_count_);
        for (std::uint32_t index = 1u; index <= variable_count_; ++index)
            internal_model_.push_back(literal {variable {index}, values_[static_cast<std::size_t>(index) << 1u] < 0});

        // The search only ever sees the formula that preprocessing left behind, so any variable a pass
        // eliminated still has to have its value re-derived from the clauses that were removed. With an empty
        // extension stack this is an identity pass.
        if (extension_stack_.size() != 0u)
        {
            model_reconstructor_.set_initial_model(internal_model_);
            const auto reconstructed = model_reconstructor_.reconstruct_full_model();
            internal_model_.assign(reconstructed.values().begin(), reconstructed.values().end());
        }
    }

    void solver_core::synchronize_search_outcome(const status solve_status) noexcept
    {
        switch (solve_status)
        {
            case status::satisfiable:
                search_coordinator_.handle_sat();
                break;
            case status::unsatisfiable:
                search_coordinator_.handle_unsat();
                break;
            case status::unknown:
            default:
                // Only stamp an external cause if the coordinator has not already recorded a specific one
                // (decision_limit/conflict_limit); otherwise this would clobber that more precise cause.
                if (search_coordinator_.current_termination_cause() == search_coordinator::termination_cause::none)
                    search_coordinator_.handle_termination();
                break;
        }
    }
}
