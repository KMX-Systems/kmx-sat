/// @file src/kmx/sat/solver.cpp
/// @brief File-level API declarations and implementation details.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <algorithm>
#include <kmx/sat/cdcl/solver_core.hpp>
#include <kmx/sat/proof/tracer/view.hpp>
#include <kmx/sat/solver.hpp>
#include <utility>
#include <vector>

namespace kmx::sat
{
    class solver::impl final
    {
    public:
        /// @brief Constructs a default-initialized instance.
        /// @throws None (noexcept).
        impl() noexcept = default;

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

        void flush_pending_clause() noexcept
        {
            if (pending_clause_.empty())
            {
                return;
            }
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

        if (impl_->terminate_callback_ && impl_->terminate_callback_())
        {
            impl_->state_machine_.transition_to_steady();
            return solve_result {solve_result::status::terminated, model_view {}, failed_core_view {}, impl_->statistics_.snapshot_of(),
                                 solve_result::proof_summary {}};
        }

        const auto core_status = impl_->core_.solve(effective_request);
        const auto mapped_status = impl_->map_status(core_status);

        impl_->last_model_.assign(impl_->core_.extract_internal_model().begin(), impl_->core_.extract_internal_model().end());
        impl_->last_failed_core_.assign(impl_->core_.extract_failed_core().begin(), impl_->core_.extract_failed_core().end());

        if (mapped_status == solve_result::status::satisfiable)
        {
            impl_->statistics_.inc("decisions");
        }
        else if (mapped_status == solve_result::status::unsatisfiable)
        {
            impl_->statistics_.inc("conflicts");
        }

        impl_->assumptions_.clear();

        if (mapped_status == solve_result::status::satisfiable)
        {
            impl_->state_machine_.transition_to_sat();
        }
        else if (mapped_status == solve_result::status::unsatisfiable)
        {
            impl_->state_machine_.transition_to_unsat();
        }
        else
        {
            impl_->state_machine_.transition_to_steady();
        }

        const model_view model {std::span<const literal> {impl_->last_model_}};
        const failed_core_view failed_core {std::span<const literal> {impl_->last_failed_core_}};
        const solve_result::proof_summary proof_summary {impl_->core_.proof_enabled(), impl_->core_.proof_checkers_valid()};

        return solve_result {mapped_status, model, failed_core, impl_->statistics_.snapshot_of(), proof_summary};
    }

    /// @brief Returns a computed or stored value from this subsystem.
    /// @param var Input argument used by this operation.
    /// @return Operation result as defined by the method contract.
    /// @throws None (noexcept).
    std::optional<bool> solver::value_of(const variable var) const noexcept
    {
        for (const auto lit: impl_->last_model_)
        {
            if (lit.variable_of().index() == var.index())
            {
                return !lit.is_negated();
            }
        }
        return std::nullopt;
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
        (void) name;
        (void) value;
        impl_->core_.persist_option_subset();
    }

    /// @brief Sets or updates subsystem configuration/state.
    /// @param profile_name Input argument used by this operation.
    /// @throws None (noexcept).
    void solver::set_configuration(const std::string_view profile_name) noexcept
    {
        (void) profile_name;
        impl_->core_.persist_option_subset();
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

    /// @brief Sets or updates subsystem configuration/state.
    /// @throws None (noexcept).
    void solver::reset_session() noexcept
    {
        impl_->core_.reset();
        if (impl_->proof_sink_ != nullptr)
        {
            impl_->core_.attach_proof_tracer(*impl_->proof_sink_);
        }
        impl_->pending_clause_.clear();
        impl_->assumptions_.clear();
        impl_->last_model_.clear();
        impl_->last_failed_core_.clear();
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
}
