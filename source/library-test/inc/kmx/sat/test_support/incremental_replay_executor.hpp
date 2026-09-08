/// @file inc/kmx/sat/test_support/incremental_replay_executor.hpp
/// @brief Executor replaying an incremental trace against a solver instance.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <optional>
    #include <vector>
#endif
#include <kmx/sat/solve_request.hpp>
#include <kmx/sat/solver.hpp>
#include <kmx/sat/test_support/incremental_replay_trace.hpp>

namespace kmx::sat::test_support
{
    /// @brief Per-query truth values reported by the replayed solver.
    using value_list_t = std::vector<std::optional<bool>>;

    struct incremental_replay_outcome final
    {
        std::vector<solve_result::status> solve_statuses {};
        value_list_t value_results {};
        std::vector<bool> failed_results {};
    };

    inline incremental_replay_outcome execute_incremental_replay(const incremental_replay_trace& trace, solver& solver)
    {
        incremental_replay_outcome outcome;
        for (const auto& operation: trace.operations)
        {
            switch (operation.kind)
            {
                case replay_operation_kind::add_clause:
                    solver.add_clause(operation.literals);
                    break;
                case replay_operation_kind::assume:
                    for (const auto lit: operation.literals)
                        solver.assume(lit);
                    break;
                case replay_operation_kind::release_assumptions:
                    solver.release_incremental_assumptions();
                    break;
                case replay_operation_kind::solve:
                {
                    solve_request request;
                    request.conflict_limit = operation.conflict_limit;
                    request.decision_limit = operation.decision_limit;
                    outcome.solve_statuses.push_back(solver.solve(request).status_of());
                    break;
                }
                case replay_operation_kind::reset_session:
                    solver.reset_session();
                    break;
                case replay_operation_kind::set_option:
                    solver.set_option(operation.option, operation.option_value);
                    break;
                case replay_operation_kind::value_of:
                    outcome.value_results.push_back(solver.value_of(operation.variable_operand));
                    break;
                case replay_operation_kind::failed:
                    outcome.failed_results.push_back(solver.failed(operation.literal_operand));
                    break;
            }
        }
        return outcome;
    }
}
