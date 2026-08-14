/// @file src/kmx/sat/c_api_adapter.cpp
/// @brief File-level API declarations and implementation details.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <cstdlib>
#include <kmx/sat/c_api_adapter.hpp>
#include <kmx/sat/ipasir.h>
#include <limits>

struct kmx_sat_ipasir_solver final
{
    kmx::sat::c_api_adapter adapter {};
};

extern "C"
{
    kmx_sat_ipasir_solver* ipasir_init(void)
    {
        return new kmx_sat_ipasir_solver {};
    }

    void ipasir_release(kmx_sat_ipasir_solver* solver)
    {
        delete solver;
    }

    void ipasir_add(kmx_sat_ipasir_solver* solver, const int lit)
    {
        if (solver != nullptr)
            solver->adapter.ipasir_add(lit);
    }

    void ipasir_assume(kmx_sat_ipasir_solver* solver, const int lit)
    {
        if (solver != nullptr)
            solver->adapter.ipasir_assume(lit);
    }

    int ipasir_solve(kmx_sat_ipasir_solver* solver)
    {
        return solver == nullptr ? 0 : solver->adapter.ipasir_solve();
    }

    int ipasir_val(const kmx_sat_ipasir_solver* solver, const int lit)
    {
        return solver == nullptr ? 0 : solver->adapter.ipasir_val(lit);
    }

    int ipasir_failed(const kmx_sat_ipasir_solver* solver, const int lit)
    {
        return solver != nullptr && solver->adapter.ipasir_failed(lit) ? 1 : 0;
    }
}

namespace kmx::sat
{
    static bool is_valid_ipasir_literal(const std::int32_t lit) noexcept
    {
        return lit != 0 && lit != std::numeric_limits<std::int32_t>::min();
    }

    static literal from_ipasir_literal(const std::int32_t lit) noexcept
    {
        if (!is_valid_ipasir_literal(lit))
            return {};
        const auto variable_index = static_cast<variable::index_t>(std::abs(lit));
        return literal {variable {variable_index}, lit < 0};
    }

    /// @brief Executes this operation on the owning subsystem.
    /// @throws None (noexcept).
    c_api_adapter::c_api_adapter() noexcept = default;

    /// @brief Releases resources owned by this instance.
    /// @throws None (noexcept).
    c_api_adapter::~c_api_adapter() noexcept = default;

    /// @brief Executes this operation on the owning subsystem.
    /// @throws None (noexcept).
    void c_api_adapter::ipasir_init() noexcept
    {
        solver_.reset_session();
    }

    /// @brief Executes this operation on the owning subsystem.
    /// @param lit Input argument used by this operation.
    /// @throws None (noexcept).
    void c_api_adapter::ipasir_add(const std::int32_t lit) noexcept
    {
        if (lit == std::numeric_limits<std::int32_t>::min())
            return;
        solver_.add_literal(from_ipasir_literal(lit));
    }

    /// @brief Executes this operation on the owning subsystem.
    /// @param lit Input argument used by this operation.
    /// @throws None (noexcept).
    void c_api_adapter::ipasir_assume(const std::int32_t lit) noexcept
    {
        if (!is_valid_ipasir_literal(lit))
            return;
        solver_.assume(from_ipasir_literal(lit));
    }

    /// @brief Executes this operation on the owning subsystem.
    /// @return Operation result as defined by the method contract.
    /// @throws None (noexcept).
    int c_api_adapter::ipasir_solve() noexcept
    {
        const auto result = solver_.solve(solve_request {});
        switch (result.status_of())
        {
            case solve_result::status::satisfiable:
                return 10;
            case solve_result::status::unsatisfiable:
                return 20;
            case solve_result::status::unknown:
            case solve_result::status::terminated:
            default:
                return 0;
        }
    }

    /// @brief Executes this operation on the owning subsystem.
    /// @param lit Input argument used by this operation.
    /// @return Operation result as defined by the method contract.
    /// @throws None (noexcept).
    std::int32_t c_api_adapter::ipasir_val(const std::int32_t lit) const noexcept
    {
        if (!is_valid_ipasir_literal(lit))
            return 0;

        const auto variable_index = static_cast<variable::index_t>(std::abs(lit));
        const auto value = solver_.value_of(variable {variable_index});
        if (!value.has_value())
            return 0;

        const bool literal_true = lit > 0 ? *value : !*value;
        return literal_true ? lit : -lit;
    }

    /// @brief Executes this operation on the owning subsystem.
    /// @param lit Input argument used by this operation.
    /// @return Operation result as defined by the method contract.
    /// @throws None (noexcept).
    bool c_api_adapter::ipasir_failed(const std::int32_t lit) const noexcept
    {
        if (!is_valid_ipasir_literal(lit))
            return false;
        return solver_.failed(from_ipasir_literal(lit));
    }

    void c_api_adapter::ipasir_set_option(const std::string_view name, const std::int64_t value) noexcept
    {
        solver_.set_option(name, value);
    }

    void c_api_adapter::ipasir_set_configuration(const std::string_view profile_name) noexcept
    {
        solver_.set_configuration(profile_name);
    }

    void c_api_adapter::ipasir_clear_persisted_configuration() noexcept
    {
        solver_.clear_persisted_configuration();
    }

    bool c_api_adapter::ipasir_persisted_option_subset() const noexcept
    {
        return solver_.persisted_option_subset();
    }

    bool c_api_adapter::ipasir_has_persisted_configuration() const noexcept
    {
        return solver_.has_persisted_configuration();
    }

    std::int64_t c_api_adapter::ipasir_configured_conflict_limit() const noexcept
    {
        const auto configured = solver_.configured_conflict_limit();
        if (!configured.has_value())
            return -1;
        return static_cast<std::int64_t>(configured.value());
    }

    std::int64_t c_api_adapter::ipasir_configured_decision_limit() const noexcept
    {
        const auto configured = solver_.configured_decision_limit();
        if (!configured.has_value())
            return -1;
        return static_cast<std::int64_t>(configured.value());
    }

    std::int32_t c_api_adapter::ipasir_configured_strict_mode() const noexcept
    {
        const auto configured = solver_.configured_strict_mode();
        if (!configured.has_value())
            return -1;
        return configured.value() ? 1 : 0;
    }

    std::string_view c_api_adapter::ipasir_configuration_profile_name() const noexcept
    {
        return solver_.configuration_profile_name();
    }

    void c_api_adapter::ipasir_set_statistics_verbose_reporting(const std::int32_t verbose) noexcept
    {
        solver_.set_statistics_report_detail(verbose == 0 ? solver::statistics_report_detail::compact :
                                                            solver::statistics_report_detail::verbose);
    }

    std::int32_t c_api_adapter::ipasir_statistics_verbose_reporting() const noexcept
    {
        return solver_.statistics_report_detail_of() == solver::statistics_report_detail::verbose ? 1 : 0;
    }

    std::string c_api_adapter::ipasir_statistics_report_line() const
    {
        return solver_.statistics_report_line();
    }

    std::size_t c_api_adapter::ipasir_statistics_report_emission_count() const noexcept
    {
        return solver_.statistics_report_emission_count();
    }

    std::string_view c_api_adapter::ipasir_last_emitted_statistics_report_line() const noexcept
    {
        return solver_.last_emitted_statistics_report_line();
    }

    std::string_view c_api_adapter::ipasir_previous_emitted_statistics_report_line() const noexcept
    {
        return solver_.previous_emitted_statistics_report_line();
    }

    std::optional<telemetry::solver_statistics::snapshot> c_api_adapter::ipasir_last_emitted_statistics_snapshot() const noexcept
    {
        return solver_.last_emitted_statistics_snapshot();
    }

    std::optional<telemetry::solver_statistics::snapshot> c_api_adapter::ipasir_previous_emitted_statistics_snapshot() const noexcept
    {
        return solver_.previous_emitted_statistics_snapshot();
    }

    std::int32_t c_api_adapter::ipasir_statistics_emission_tail_monotonic() const noexcept
    {
        return solver_.emitted_statistics_snapshot_tail_monotonic() ? 1 : 0;
    }

    std::optional<telemetry::solver_statistics::snapshot> c_api_adapter::ipasir_statistics_emission_tail_delta() const noexcept
    {
        return solver_.emitted_statistics_snapshot_tail_delta();
    }
}
