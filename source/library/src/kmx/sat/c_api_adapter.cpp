/// @file src/kmx/sat/c_api_adapter.cpp
/// @brief File-level API declarations and implementation details.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <cstdlib>
#include <kmx/sat/c_api_adapter.hpp>

namespace kmx::sat
{
    namespace
    {
        literal from_ipasir_literal(const std::int32_t lit) noexcept
        {
            if (lit == 0)
            {
                return {};
            }
            const auto variable_index = static_cast<variable::index_t>(std::abs(lit));
            return literal {variable {variable_index}, lit < 0};
        }
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
        solver_.add_literal(from_ipasir_literal(lit));
    }

    /// @brief Executes this operation on the owning subsystem.
    /// @param lit Input argument used by this operation.
    /// @throws None (noexcept).
    void c_api_adapter::ipasir_assume(const std::int32_t lit) noexcept
    {
        if (lit == 0)
        {
            return;
        }
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
        if (lit == 0)
        {
            return 0;
        }

        const auto variable_index = static_cast<variable::index_t>(std::abs(lit));
        const auto value = solver_.value_of(variable {variable_index});
        if (!value.has_value())
        {
            return 0;
        }

        const bool literal_true = lit > 0 ? *value : !*value;
        return literal_true ? lit : -lit;
    }

    /// @brief Executes this operation on the owning subsystem.
    /// @param lit Input argument used by this operation.
    /// @return Operation result as defined by the method contract.
    /// @throws None (noexcept).
    bool c_api_adapter::ipasir_failed(const std::int32_t lit) const noexcept
    {
        if (lit == 0)
        {
            return false;
        }
        return solver_.failed(from_ipasir_literal(lit));
    }
}
