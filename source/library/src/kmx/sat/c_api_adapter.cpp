/// @file src/kmx/sat/c_api_adapter.cpp
/// @brief File-level API declarations and implementation details.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/c_api_adapter.hpp>

namespace kmx::sat
{
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
    }

    /// @brief Executes this operation on the owning subsystem.
    /// @param lit Input argument used by this operation.
    /// @throws None (noexcept).
    void c_api_adapter::ipasir_add(const std::int32_t lit) noexcept
    {
    }

    /// @brief Executes this operation on the owning subsystem.
    /// @param lit Input argument used by this operation.
    /// @throws None (noexcept).
    void c_api_adapter::ipasir_assume(const std::int32_t lit) noexcept
    {
    }

    /// @brief Executes this operation on the owning subsystem.
    /// @return Operation result as defined by the method contract.
    /// @throws None (noexcept).
    int c_api_adapter::ipasir_solve() noexcept
    {
        return 0;
    }

    /// @brief Executes this operation on the owning subsystem.
    /// @param lit Input argument used by this operation.
    /// @return Operation result as defined by the method contract.
    /// @throws None (noexcept).
    std::int32_t c_api_adapter::ipasir_val(const std::int32_t lit) const noexcept
    {
        return 0;
    }

    /// @brief Executes this operation on the owning subsystem.
    /// @param lit Input argument used by this operation.
    /// @return Operation result as defined by the method contract.
    /// @throws None (noexcept).
    bool c_api_adapter::ipasir_failed(const std::int32_t lit) const noexcept
    {
        return false;
    }
}
