/// @file src/kmx/sat/solver.cpp
/// @brief File-level API declarations and implementation details.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <algorithm>
#include <cstdlib>
#include <kmx/sat/cdcl/solver_core.hpp>
#include <kmx/sat/io/writer/format.hpp>
#include <kmx/sat/proof/tracer/view.hpp>
#include <kmx/sat/solver.hpp>
#include <string>
#include <utility>
#include <vector>

namespace kmx::sat
{
    static std::optional<literal> literal_from_dimacs(const int32_t dimacs_value) noexcept
    {
        if (dimacs_value == 0)
            return {};

        const auto variable_index = static_cast<variable::index_t>(std::abs(dimacs_value));
        return literal {variable {variable_index}, dimacs_value < 0};
    }

    class solver::impl final
    {
    public:
        /// @brief Constructs a default-initialized instance.
        /// @throws None (noexcept).
        impl() noexcept { apply_core_configuration(); }

        static constexpr std::uint32_t default_decision_conflict_maintenance_interval {16u};
        static constexpr std::uint32_t default_decision_chb_decay_interval {8u};
        static constexpr std::uint32_t default_decision_restart_decay_interval {4u};

        cdcl::solver_core core_ {};
        telemetry::solver_statistics statistics_ {};
        std::vector<literal> pending_clause_ {};
        std::vector<literal> assumptions_ {};
        std::vector<literal> last_model_ {};
        std::vector<literal> last_failed_core_ {};
        solver_state_machine state_machine_ {};
        terminate_callback_t terminate_callback_ {};
        learn_callback_t learn_callback_ {};
        external_propagator_hook_t external_propagator_hook_ {};
        proof::tracer::view* proof_sink_ {};
        std::uint64_t configured_conflict_limit_ {};
        std::uint64_t configured_decision_limit_ {};
        std::uint64_t configured_enabled_pass_mask_ {};
        std::uint32_t configured_decision_conflict_maintenance_interval_ {};
        std::uint32_t configured_decision_chb_decay_interval_ {};
        std::uint32_t configured_decision_restart_decay_interval_ {};
        std::uint64_t configured_restart_interval_ {};
        std::uint64_t configured_decision_restart_interval_ {};
        std::uint64_t configured_reduction_interval_ {};
        std::uint32_t configured_reduction_fraction_percent_ {50u};
        double configured_activity_retention_threshold_ {2.0};
        bool configured_chb_enabled_ {};
        std::uint32_t configured_glue_restart_threshold_percent_ {};
        bool configured_cold_storage_enabled_ {};
        bool configured_strict_mode_ {};
        bool has_configured_conflict_limit_ {};
        bool has_configured_decision_limit_ {};
        bool has_configured_enabled_pass_mask_ {};
        bool has_configured_decision_conflict_maintenance_interval_ {};
        bool has_configured_decision_chb_decay_interval_ {};
        bool has_configured_decision_restart_decay_interval_ {};
        bool has_configured_restart_interval_ {};
        bool has_configured_decision_restart_interval_ {};
        bool has_configured_reduction_interval_ {};
        bool has_configured_reduction_fraction_percent_ {};
        bool has_configured_activity_retention_threshold_ {};
        bool has_configured_chb_enabled_ {};
        bool has_configured_glue_restart_threshold_percent_ {};
        bool has_configured_cold_storage_enabled_ {};
        bool has_configured_strict_mode_ {};
        solver::statistics_report_detail statistics_report_detail_ {solver::statistics_report_detail::compact};
        std::string active_configuration_profile_ {};
        std::size_t last_consumed_proof_event_count_ {};
        std::vector<std::string> emitted_statistics_report_lines_ {};
        std::vector<telemetry::solver_statistics::snapshot> emitted_statistics_snapshots_ {};

        void emit_statistics_report_checkpoint() noexcept
        {
            io::writer::format formatter {};
            const auto snapshot = statistics_.snapshot_of();
            const auto detail = statistics_report_detail_ == solver::statistics_report_detail::verbose ?
                                    io::writer::format::statistics_detail::verbose :
                                    io::writer::format::statistics_detail::compact;
            formatter.write_statistics(snapshot, detail);
            emitted_statistics_report_lines_.emplace_back(formatter.buffer_view());
            emitted_statistics_snapshots_.push_back(snapshot);
            constexpr std::size_t max_emitted_statistics_report_lines = 64u;
            if (emitted_statistics_report_lines_.size() > max_emitted_statistics_report_lines)
                emitted_statistics_report_lines_.erase(emitted_statistics_report_lines_.begin());
            if (emitted_statistics_snapshots_.size() > max_emitted_statistics_report_lines)
                emitted_statistics_snapshots_.erase(emitted_statistics_snapshots_.begin());
        }

        void flush_pending_clause() noexcept
        {
            if (pending_clause_.empty())
                return;
            core_.add_problem_clause(std::span<const literal> {pending_clause_});
            pending_clause_.clear();
        }

        static solve_result::status map_status(const cdcl::solver_core::status status_value) noexcept
        {
            switch (status_value)
            {
                case cdcl::solver_core::status::satisfiable:
                    return solve_result::status::satisfiable;
                case cdcl::solver_core::status::unsatisfiable:
                    return solve_result::status::unsatisfiable;
                case cdcl::solver_core::status::unknown:
                default:
                    return solve_result::status::unknown;
            }
        }

        [[nodiscard]] bool has_persisted_configuration() const noexcept
        {
            return has_configured_conflict_limit_ || has_configured_decision_limit_ || has_configured_enabled_pass_mask_ ||
                   has_configured_decision_conflict_maintenance_interval_ || has_configured_decision_chb_decay_interval_ ||
                   has_configured_decision_restart_decay_interval_ || has_configured_restart_interval_ ||
                   has_configured_decision_restart_interval_ || has_configured_reduction_fraction_percent_ || has_configured_chb_enabled_ ||
                   has_configured_reduction_interval_ || has_configured_glue_restart_threshold_percent_ ||
                   has_configured_cold_storage_enabled_ || has_configured_activity_retention_threshold_ || has_configured_strict_mode_ ||
                   !active_configuration_profile_.empty();
        }

        void clear_persisted_configuration() noexcept
        {
            configured_conflict_limit_ = 0u;
            configured_decision_limit_ = 0u;
            configured_enabled_pass_mask_ = 0u;
            configured_decision_conflict_maintenance_interval_ = 0u;
            configured_decision_chb_decay_interval_ = 0u;
            configured_decision_restart_decay_interval_ = 0u;
            configured_restart_interval_ = 0u;
            configured_decision_restart_interval_ = 0u;
            configured_reduction_interval_ = 0u;
            configured_reduction_fraction_percent_ = 50u;
            configured_activity_retention_threshold_ = 2.0;
            configured_chb_enabled_ = false;
            configured_glue_restart_threshold_percent_ = 0u;
            configured_cold_storage_enabled_ = false;
            configured_strict_mode_ = false;
            has_configured_conflict_limit_ = false;
            has_configured_decision_limit_ = false;
            has_configured_enabled_pass_mask_ = false;
            has_configured_decision_conflict_maintenance_interval_ = false;
            has_configured_decision_chb_decay_interval_ = false;
            has_configured_decision_restart_decay_interval_ = false;
            has_configured_restart_interval_ = false;
            has_configured_decision_restart_interval_ = false;
            has_configured_reduction_interval_ = false;
            has_configured_reduction_fraction_percent_ = false;
            has_configured_activity_retention_threshold_ = false;
            has_configured_chb_enabled_ = false;
            has_configured_glue_restart_threshold_percent_ = false;
            has_configured_cold_storage_enabled_ = false;
            has_configured_strict_mode_ = false;
            active_configuration_profile_.clear();
        }

        void apply_core_configuration() noexcept
        {
            const auto conflict_maintenance_interval = has_configured_decision_conflict_maintenance_interval_ ?
                                                           configured_decision_conflict_maintenance_interval_ :
                                                           default_decision_conflict_maintenance_interval;
            const auto chb_decay_interval =
                has_configured_decision_chb_decay_interval_ ? configured_decision_chb_decay_interval_ : default_decision_chb_decay_interval;
            const auto restart_decay_interval = has_configured_decision_restart_decay_interval_ ?
                                                    configured_decision_restart_decay_interval_ :
                                                    default_decision_restart_decay_interval;

            core_.set_decision_maintenance_intervals(conflict_maintenance_interval, chb_decay_interval, restart_decay_interval);
            core_.set_restart_interval(has_configured_restart_interval_ ? configured_restart_interval_ : 0u);
            core_.set_decision_restart_interval(has_configured_decision_restart_interval_ ? configured_decision_restart_interval_ : 0u);
            core_.set_reduction_interval(has_configured_reduction_interval_ ? configured_reduction_interval_ : 0u);
            core_.set_chb_enabled(configured_chb_enabled_);
            core_.set_reduction_fraction_percent(has_configured_reduction_fraction_percent_ ? configured_reduction_fraction_percent_ : 50u);
            core_.set_activity_retention_threshold(has_configured_activity_retention_threshold_ ? configured_activity_retention_threshold_ :
                                                                                                  2.0);
            core_.set_glue_restart_threshold_percent(
                has_configured_glue_restart_threshold_percent_ ? configured_glue_restart_threshold_percent_ : 0u);
            core_.set_cold_storage_enabled(configured_cold_storage_enabled_);
        }

        void apply_persisted_configuration(solve_request& request) const noexcept
        {
            if (request.conflict_limit == 0u && has_configured_conflict_limit_)
                request.conflict_limit = configured_conflict_limit_;
            if (request.decision_limit == 0u && has_configured_decision_limit_)
                request.decision_limit = configured_decision_limit_;
            if (request.enabled_pass_mask == 0u && has_configured_enabled_pass_mask_)
                request.enabled_pass_mask = configured_enabled_pass_mask_;
            if (!request.strict_mode && has_configured_strict_mode_)
                request.strict_mode = configured_strict_mode_;
        }
    };

    solver::solver() noexcept: impl_ {new impl {}}
    {
    }

    /// @brief Releases resources owned by this instance.
    /// @throws None (noexcept).
    solver::~solver() noexcept
    {
        delete impl_;
    }

    solver::solver(solver&& other) noexcept: impl_ {other.impl_}
    {
        other.impl_ = nullptr;
    }

    /// @brief Constructs a default-initialized instance.
    /// @param other Input argument used by this operation.
    /// @return Operation result as defined by the method contract.
    /// @throws None (noexcept).
    solver& solver::operator=(solver&& other) noexcept
    {
        if (this != &other)
        {
            delete impl_;
            impl_ = other.impl_;
            other.impl_ = nullptr;
        }
        return *this;
    }

    /// @brief Constructs a default-initialized instance.
    /// @param variable_count Input argument used by this operation.
    /// @throws None (noexcept).
    void solver::reserve(const variable::index_t variable_count) noexcept
    {
        impl_->pending_clause_.reserve(variable_count);
        impl_->assumptions_.reserve(variable_count);
    }

    /// @brief Adds or registers data in the subsystem.
    /// @param lit Input argument used by this operation.
    /// @throws None (noexcept).
    void solver::add_literal(const literal lit) noexcept
    {
        if (lit.raw() == 0)
        {
            if (impl_->pending_clause_.empty())
                impl_->core_.add_problem_clause(std::span<const literal> {});
            else
                impl_->flush_pending_clause();
            impl_->state_machine_.transition_to_adding();
            return;
        }
        impl_->pending_clause_.push_back(lit);
        impl_->state_machine_.transition_to_adding();
    }

    /// @brief Adds or registers data in the subsystem.
    /// @param clause Input argument used by this operation.
    /// @throws None (noexcept).
    void solver::add_clause(const std::span<const literal> clause) noexcept
    {
        impl_->flush_pending_clause();
        impl_->core_.add_problem_clause(clause);
        impl_->state_machine_.transition_to_adding();
    }

    /// @brief Constructs a default-initialized instance.
    /// @param lit Input argument used by this operation.
    /// @throws None (noexcept).
    void solver::assume(const literal lit) noexcept
    {
        impl_->assumptions_.push_back(lit);
        impl_->state_machine_.transition_to_adding();
    }

    /// @brief Constructs a default-initialized instance.
    /// @param request Input argument used by this operation.
    /// @return Operation result as defined by the method contract.
    /// @throws May propagate implementation-defined exceptions from dependent operations.
    solve_result solver::solve(const solve_request& request)
    {
        impl_->flush_pending_clause();

        impl_->state_machine_.transition_to_solving();

        solve_request effective_request {request};
        effective_request.assumptions.insert(effective_request.assumptions.end(), impl_->assumptions_.begin(), impl_->assumptions_.end());
        impl_->apply_persisted_configuration(effective_request);
        impl_->emit_statistics_report_checkpoint();

        if (impl_->terminate_callback_ && impl_->terminate_callback_())
        {
            impl_->statistics_.inc("terminate_callback_calls");
            impl_->emit_statistics_report_checkpoint();
            impl_->state_machine_.transition_to_steady();
            return solve_result {solve_result::status::terminated, model_view {}, failed_core_view {}, impl_->statistics_.snapshot_of(),
                                 solve_result::proof_summary {}};
        }
        if (impl_->terminate_callback_)
            impl_->statistics_.inc("terminate_callback_calls");

        if (impl_->external_propagator_hook_)
        {
            impl_->external_propagator_hook_();
            impl_->statistics_.inc("external_propagator_calls");
        }

        const auto core_status = impl_->core_.solve(effective_request);
        const auto mapped_status = impl_->map_status(core_status);
        impl_->statistics_.add("propagations", static_cast<std::uint64_t>(impl_->core_.propagation_assignment_count()));

        impl_->last_model_.assign(impl_->core_.extract_internal_model().begin(), impl_->core_.extract_internal_model().end());
        impl_->last_failed_core_.assign(impl_->core_.extract_failed_core().begin(), impl_->core_.extract_failed_core().end());

        impl_->statistics_.add("conflicts", impl_->core_.conflict_event_count());
        impl_->statistics_.add("decisions", impl_->core_.decision_event_count());
        impl_->statistics_.add("restarts", impl_->core_.restart_count());
        impl_->statistics_.add("learned_clauses", impl_->core_.episode_learned_clause_count());
        impl_->statistics_.add("learned_clause_glue_total", impl_->core_.learned_clause_glue_total());
        impl_->statistics_.add("learned_clause_glue_samples", impl_->core_.learned_clause_glue_sample_count());
        impl_->statistics_.add("reduction_passes", impl_->core_.reduction_pass_count());
        impl_->statistics_.add("reduced_clauses", impl_->core_.reduced_clause_count());
        impl_->statistics_.add("deleted_clauses", impl_->core_.deleted_clause_count());

        if (impl_->learn_callback_)
        {
            std::size_t learn_callback_count = 0u;
            std::vector<literal> learned_clause_literals {};

            const auto buffered_events = impl_->core_.buffered_proof_events();
            if (impl_->last_consumed_proof_event_count_ > buffered_events.size())
                impl_->last_consumed_proof_event_count_ = 0u;

            for (std::size_t index = impl_->last_consumed_proof_event_count_; index < buffered_events.size(); ++index)
            {
                const auto& event = buffered_events[index];
                if (event.kind != proof::event_kind::add_derived || event.literals.empty())
                    continue;

                learned_clause_literals.clear();
                learned_clause_literals.reserve(event.literals.size());
                for (const auto dimacs_literal: event.literals)
                {
                    const auto parsed_literal = literal_from_dimacs(dimacs_literal);
                    if (parsed_literal.has_value())
                        learned_clause_literals.push_back(parsed_literal.value());
                }

                if (learned_clause_literals.empty())
                    continue;

                impl_->learn_callback_(std::span<const literal> {learned_clause_literals});
                ++learn_callback_count;
                impl_->statistics_.inc("learn_callback_calls");
            }

            impl_->last_consumed_proof_event_count_ = buffered_events.size();

            if (learn_callback_count == 0u && mapped_status == solve_result::status::unsatisfiable && !impl_->last_failed_core_.empty())
            {
                impl_->learn_callback_(std::span<const literal> {impl_->last_failed_core_});
                ++learn_callback_count;
                impl_->statistics_.inc("learn_callback_calls");
            }
        }

        impl_->assumptions_.clear();

        if (mapped_status == solve_result::status::satisfiable)
            impl_->state_machine_.transition_to_sat();
        else if (mapped_status == solve_result::status::unsatisfiable)
            impl_->state_machine_.transition_to_unsat();
        else
            impl_->state_machine_.transition_to_steady();

        impl_->core_.finalize_proof();

        const model_view model {std::span<const literal> {impl_->last_model_}};
        const failed_core_view failed_core {std::span<const literal> {impl_->last_failed_core_}};
        const solve_result::proof_summary proof_summary {impl_->core_.proof_enabled(), impl_->core_.proof_checkers_valid()};
        impl_->emit_statistics_report_checkpoint();

        return solve_result {mapped_status, model, failed_core, impl_->statistics_.snapshot_of(), proof_summary};
    }

    /// @brief Returns a computed or stored value from this subsystem.
    /// @param var Input argument used by this operation.
    /// @return Operation result as defined by the method contract.
    /// @throws None (noexcept).
    std::optional<bool> solver::value_of(const variable var) const noexcept
    {
        for (const auto lit: impl_->last_model_)
            if (lit.variable_of().index() == var.index())
                return !lit.is_negated();
        return {};
    }

    /// @brief Constructs a default-initialized instance.
    /// @param lit Input argument used by this operation.
    /// @return Operation result as defined by the method contract.
    /// @throws None (noexcept).
    bool solver::failed(const literal lit) const noexcept
    {
        return std::any_of(impl_->last_failed_core_.begin(), impl_->last_failed_core_.end(),
                           [&](const literal failed_lit) noexcept { return failed_lit.raw() == lit.raw(); });
    }

    /// @brief Sets or updates subsystem configuration/state.
    /// @param name Input argument used by this operation.
    /// @param value Input argument used by this operation.
    /// @throws None (noexcept).
    void solver::set_option(const std::string_view name, const std::int64_t value) noexcept
    {
        bool recognized_option = false;
        if (name == "conflict_limit")
        {
            recognized_option = true;
            impl_->has_configured_conflict_limit_ = value >= 0;
            impl_->configured_conflict_limit_ = value >= 0 ? static_cast<std::uint64_t>(value) : 0u;
        }
        else if (name == "decision_limit")
        {
            recognized_option = true;
            impl_->has_configured_decision_limit_ = value >= 0;
            impl_->configured_decision_limit_ = value >= 0 ? static_cast<std::uint64_t>(value) : 0u;
        }
        else if (name == "enabled_pass_mask")
        {
            recognized_option = true;
            impl_->has_configured_enabled_pass_mask_ = value >= 0;
            impl_->configured_enabled_pass_mask_ = value >= 0 ? static_cast<std::uint64_t>(value) : 0u;
        }
        else if (name == "strict_mode")
        {
            recognized_option = true;
            impl_->has_configured_strict_mode_ = true;
            impl_->configured_strict_mode_ = value != 0;
        }
        else if (name == "statistics_verbose_reporting")
        {
            recognized_option = true;
            impl_->statistics_report_detail_ = value == 0 ? statistics_report_detail::compact : statistics_report_detail::verbose;
        }
        else if (name == "decision_conflict_maintenance_interval")
        {
            recognized_option = true;
            impl_->has_configured_decision_conflict_maintenance_interval_ = value >= 0;
            impl_->configured_decision_conflict_maintenance_interval_ = value >= 0 ? static_cast<std::uint32_t>(value) : 0u;
        }
        else if (name == "decision_chb_decay_interval")
        {
            recognized_option = true;
            impl_->has_configured_decision_chb_decay_interval_ = value >= 0;
            impl_->configured_decision_chb_decay_interval_ = value >= 0 ? static_cast<std::uint32_t>(value) : 0u;
        }
        else if (name == "decision_restart_decay_interval")
        {
            recognized_option = true;
            impl_->has_configured_decision_restart_decay_interval_ = value >= 0;
            impl_->configured_decision_restart_decay_interval_ = value >= 0 ? static_cast<std::uint32_t>(value) : 0u;
        }
        else if (name == "restart_interval")
        {
            recognized_option = true;
            impl_->has_configured_restart_interval_ = value >= 0;
            impl_->configured_restart_interval_ = value >= 0 ? static_cast<std::uint64_t>(value) : 0u;
        }
        else if (name == "decision_restart_interval")
        {
            recognized_option = true;
            impl_->has_configured_decision_restart_interval_ = value >= 0;
            impl_->configured_decision_restart_interval_ = value >= 0 ? static_cast<std::uint64_t>(value) : 0u;
        }
        else if (name == "reduction_interval")
        {
            recognized_option = true;
            impl_->has_configured_reduction_interval_ = value >= 0;
            impl_->configured_reduction_interval_ = value >= 0 ? static_cast<std::uint64_t>(value) : 0u;
        }
        else if (name == "chb_enabled")
        {
            recognized_option = true;
            impl_->has_configured_chb_enabled_ = true;
            impl_->configured_chb_enabled_ = value != 0;
        }
        else if (name == "reduction_fraction_percent")
        {
            recognized_option = true;
            impl_->has_configured_reduction_fraction_percent_ = value >= 1 && value <= 100;
            if (impl_->has_configured_reduction_fraction_percent_)
                impl_->configured_reduction_fraction_percent_ = static_cast<std::uint32_t>(value);
        }
        else if (name == "glue_restart_threshold_percent")
        {
            recognized_option = true;
            impl_->has_configured_glue_restart_threshold_percent_ = value == 0 || value >= 101;
            if (impl_->has_configured_glue_restart_threshold_percent_)
                impl_->configured_glue_restart_threshold_percent_ = value >= 0 ? static_cast<std::uint32_t>(value) : 0u;
        }
        else if (name == "cold_storage_enabled")
        {
            recognized_option = true;
            impl_->has_configured_cold_storage_enabled_ = true;
            impl_->configured_cold_storage_enabled_ = value != 0;
        }
        else if (name == "activity_retention_threshold_percent")
        {
            recognized_option = true;
            impl_->has_configured_activity_retention_threshold_ = value >= 0;
            impl_->configured_activity_retention_threshold_ = value >= 0 ? static_cast<double>(value) / 100.0 : 2.0;
        }

        if (recognized_option)
        {
            impl_->apply_core_configuration();
            impl_->core_.persist_option_subset();
            impl_->statistics_.inc("option_updates");
        }
    }

    /// @brief Sets or updates subsystem configuration/state.
    /// @param profile_name Input argument used by this operation.
    /// @throws None (noexcept).
    void solver::set_configuration(const std::string_view profile_name) noexcept
    {
        impl_->clear_persisted_configuration();

        if (profile_name == "safe")
        {
            impl_->active_configuration_profile_ = "safe";
            impl_->has_configured_strict_mode_ = true;
            impl_->configured_strict_mode_ = true;
            impl_->has_configured_enabled_pass_mask_ = true;
            impl_->configured_enabled_pass_mask_ = 0u;
            impl_->core_.persist_option_subset();
            impl_->statistics_.inc("configuration_updates");
        }
        else if (profile_name == "balanced")
        {
            impl_->active_configuration_profile_ = "balanced";
            impl_->has_configured_strict_mode_ = true;
            impl_->configured_strict_mode_ = false;
            impl_->has_configured_enabled_pass_mask_ = true;
            impl_->configured_enabled_pass_mask_ = ~0ull;
            impl_->core_.persist_option_subset();
            impl_->statistics_.inc("configuration_updates");
        }
        else if (profile_name == "bounded")
        {
            impl_->active_configuration_profile_ = "bounded";
            impl_->has_configured_conflict_limit_ = true;
            impl_->configured_conflict_limit_ = 1000u;
            impl_->has_configured_decision_limit_ = true;
            impl_->configured_decision_limit_ = 10000u;
            impl_->core_.persist_option_subset();
            impl_->statistics_.inc("configuration_updates");
        }
        else if (profile_name == "aggressive")
        {
            impl_->active_configuration_profile_ = "aggressive";
            impl_->has_configured_strict_mode_ = true;
            impl_->configured_strict_mode_ = false;
            impl_->has_configured_enabled_pass_mask_ = true;
            impl_->configured_enabled_pass_mask_ = ~0ull;
            impl_->has_configured_conflict_limit_ = true;
            impl_->configured_conflict_limit_ = 0u;
            impl_->has_configured_decision_limit_ = true;
            impl_->configured_decision_limit_ = 0u;
            impl_->core_.persist_option_subset();
            impl_->statistics_.inc("configuration_updates");
        }

        impl_->apply_core_configuration();
    }

    /// @brief Adds or registers data in the subsystem.
    /// @param sink Input argument used by this operation.
    /// @throws None (noexcept).
    void solver::attach_proof_sink(proof::tracer::view& sink) noexcept
    {
        impl_->proof_sink_ = &sink;
        impl_->core_.attach_proof_tracer(sink);
    }

    /// @brief Sets or updates subsystem configuration/state.
    /// @param callback Input argument used by this operation.
    /// @throws None (noexcept).
    void solver::set_terminate(terminate_callback_t callback) noexcept
    {
        impl_->terminate_callback_ = std::move(callback);
    }

    /// @brief Sets or updates subsystem configuration/state.
    /// @param callback Input argument used by this operation.
    /// @throws None (noexcept).
    void solver::set_learn(learn_callback_t callback) noexcept
    {
        impl_->learn_callback_ = std::move(callback);
    }

    /// @brief Sets or updates subsystem configuration/state.
    /// @param hook Input argument used by this operation.
    /// @throws None (noexcept).
    void solver::set_external_propagator(external_propagator_hook_t hook) noexcept
    {
        impl_->external_propagator_hook_ = std::move(hook);
    }

    /// @brief Constructs a default-initialized instance.
    /// @return Operation result as defined by the method contract.
    /// @throws None (noexcept).
    telemetry::solver_statistics::snapshot solver::statistics() const noexcept
    {
        return impl_->statistics_.snapshot_of();
    }

    void solver::set_statistics_report_detail(const statistics_report_detail detail) noexcept
    {
        impl_->statistics_report_detail_ = detail;
    }

    solver::statistics_report_detail solver::statistics_report_detail_of() const noexcept
    {
        return impl_->statistics_report_detail_;
    }

    std::string solver::statistics_report_line() const
    {
        io::writer::format formatter {};
        const auto detail = impl_->statistics_report_detail_ == statistics_report_detail::verbose ?
                                io::writer::format::statistics_detail::verbose :
                                io::writer::format::statistics_detail::compact;
        formatter.write_statistics(impl_->statistics_.snapshot_of(), detail);
        return std::string {formatter.buffer_view()};
    }

    std::size_t solver::statistics_report_emission_count() const noexcept
    {
        return impl_->emitted_statistics_report_lines_.size();
    }

    std::string_view solver::last_emitted_statistics_report_line() const noexcept
    {
        if (impl_->emitted_statistics_report_lines_.empty())
            return {};
        return impl_->emitted_statistics_report_lines_.back();
    }

    std::string_view solver::previous_emitted_statistics_report_line() const noexcept
    {
        if (impl_->emitted_statistics_report_lines_.size() < 2u)
            return {};
        return impl_->emitted_statistics_report_lines_[impl_->emitted_statistics_report_lines_.size() - 2u];
    }

    std::optional<telemetry::solver_statistics::snapshot> solver::last_emitted_statistics_snapshot() const noexcept
    {
        if (impl_->emitted_statistics_snapshots_.empty())
            return {};
        return impl_->emitted_statistics_snapshots_.back();
    }

    std::optional<telemetry::solver_statistics::snapshot> solver::previous_emitted_statistics_snapshot() const noexcept
    {
        if (impl_->emitted_statistics_snapshots_.size() < 2u)
            return {};
        return impl_->emitted_statistics_snapshots_[impl_->emitted_statistics_snapshots_.size() - 2u];
    }

    bool solver::emitted_statistics_snapshot_tail_monotonic() const noexcept
    {
        const auto previous = previous_emitted_statistics_snapshot();
        const auto latest = last_emitted_statistics_snapshot();
        if (!previous.has_value() || !latest.has_value())
            return true;
        return telemetry::solver_statistics::snapshot_monotonic(previous.value(), latest.value());
    }

    std::optional<telemetry::solver_statistics::snapshot> solver::emitted_statistics_snapshot_tail_delta() const noexcept
    {
        const auto previous = previous_emitted_statistics_snapshot();
        const auto latest = last_emitted_statistics_snapshot();
        if (!previous.has_value() || !latest.has_value())
            return {};
        return telemetry::solver_statistics::snapshot_delta_between(previous.value(), latest.value());
    }

    /// @brief Sets or updates subsystem configuration/state.
    /// @throws None (noexcept).
    void solver::reset_session() noexcept
    {
        impl_->core_.reset();
        impl_->apply_core_configuration();
        if (impl_->has_persisted_configuration())
            impl_->core_.persist_option_subset();
        if (impl_->proof_sink_ != nullptr)
            impl_->core_.attach_proof_tracer(*impl_->proof_sink_);
        impl_->pending_clause_.clear();
        impl_->assumptions_.clear();
        impl_->last_model_.clear();
        impl_->last_failed_core_.clear();
        impl_->last_consumed_proof_event_count_ = 0u;
        impl_->emitted_statistics_report_lines_.clear();
        impl_->emitted_statistics_snapshots_.clear();
        impl_->statistics_.reset_epoch_counters();
        impl_->state_machine_ = solver_state_machine {};
    }

    /// @brief Executes this operation on the owning subsystem.
    /// @throws None (noexcept).
    void solver::release_incremental_assumptions() noexcept
    {
        impl_->assumptions_.clear();
    }

    solver_state_machine::state solver::current_state() const noexcept
    {
        return impl_->state_machine_.current_state();
    }

    bool solver::persisted_option_subset() const noexcept
    {
        return impl_->core_.persisted_option_subset();
    }

    std::size_t solver::proof_buffered_event_count() const noexcept
    {
        return impl_->core_.proof_buffered_event_count();
    }

    std::size_t solver::proof_buffered_payload_bytes() const noexcept
    {
        std::size_t bytes = 0u;
        for (const auto& event: impl_->core_.buffered_proof_events())
        {
            bytes += sizeof(proof::proof_event);
            bytes += event.literals.capacity() * sizeof(std::int32_t);
            bytes += event.antecedent_ids.capacity() * sizeof(proof::clause::id);
        }
        return bytes;
    }

    std::span<const proof::proof_event> solver::buffered_proof_events() const noexcept
    {
        return impl_->core_.buffered_proof_events();
    }

    bool solver::has_persisted_configuration() const noexcept
    {
        return impl_->has_persisted_configuration();
    }

    std::array<std::uint32_t, 3> solver::decision_maintenance_intervals() const noexcept
    {
        return impl_->core_.decision_maintenance_intervals();
    }

    bool solver::chb_enabled() const noexcept
    {
        return impl_->core_.chb_enabled();
    }

    std::uint32_t solver::reduction_fraction_percent() const noexcept
    {
        return impl_->core_.reduction_fraction_percent();
    }

    double solver::activity_retention_threshold() const noexcept
    {
        return impl_->core_.activity_retention_threshold();
    }

    std::uint32_t solver::glue_restart_threshold_percent() const noexcept
    {
        return impl_->core_.glue_restart_threshold_percent();
    }

    bool solver::cold_storage_enabled() const noexcept
    {
        return impl_->core_.cold_storage_enabled();
    }

    std::size_t solver::cold_footprint_bytes() const noexcept
    {
        return impl_->core_.cold_footprint_bytes();
    }

    std::optional<std::uint64_t> solver::configured_conflict_limit() const noexcept
    {
        if (!impl_->has_configured_conflict_limit_)
            return {};
        return impl_->configured_conflict_limit_;
    }

    std::optional<std::uint64_t> solver::configured_decision_limit() const noexcept
    {
        if (!impl_->has_configured_decision_limit_)
            return {};
        return impl_->configured_decision_limit_;
    }

    std::optional<std::uint64_t> solver::configured_enabled_pass_mask() const noexcept
    {
        if (!impl_->has_configured_enabled_pass_mask_)
            return {};
        return impl_->configured_enabled_pass_mask_;
    }

    std::optional<bool> solver::configured_strict_mode() const noexcept
    {
        if (!impl_->has_configured_strict_mode_)
            return {};
        return impl_->configured_strict_mode_;
    }

    std::string_view solver::configuration_profile_name() const noexcept
    {
        return impl_->active_configuration_profile_;
    }

    void solver::clear_persisted_configuration() noexcept
    {
        impl_->clear_persisted_configuration();
    }
}
