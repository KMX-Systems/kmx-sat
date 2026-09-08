#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>

#include <kmx/sat/solver_state_machine.hpp>
#include <kmx/sat/test_support/incremental_replay_executor.hpp>
#include <kmx/sat/test_support/incremental_replay_trace.hpp>

namespace kmx::sat
{
    TEST_CASE("incremental replay trace serializes a versioned operation stream", "[sat]")
    {
        using test_support::incremental_replay_operation;
        using test_support::incremental_replay_trace;
        using test_support::replay_operation_kind;

        incremental_replay_trace trace;
        trace.seed = 20260814u;
        trace.variable_count = 4u;
        trace.operations = {
            incremental_replay_operation {.kind = replay_operation_kind::add_clause,
                                          .literals = {literal {variable {1u}, false}, literal {variable {2u}, true}}},
            incremental_replay_operation {.kind = replay_operation_kind::assume, .literals = {literal {variable {1u}, true}}},
            incremental_replay_operation {.kind = replay_operation_kind::solve, .conflict_limit = 3u, .decision_limit = 7u},
            incremental_replay_operation {.kind = replay_operation_kind::release_assumptions},
            incremental_replay_operation {.kind = replay_operation_kind::reset_session},
            incremental_replay_operation {.kind = replay_operation_kind::set_option, .option = option_id::chb_enabled, .option_value = 1L},
            incremental_replay_operation {.kind = replay_operation_kind::value_of, .variable_operand = variable {2u}},
            incremental_replay_operation {.kind = replay_operation_kind::failed, .literal_operand = literal {variable {1u}, true}},
        };

        const auto serialized = trace.serialize_json_lines();
        REQUIRE(serialized.find("\"schema\":1") != std::string::npos);
        REQUIRE(serialized.find("\"seed\":20260814") != std::string::npos);
        REQUIRE(serialized.find("\"kind\":\"add_clause\"") != std::string::npos);
        REQUIRE(serialized.find("\"literals\":[1,-2]") != std::string::npos);
        REQUIRE(serialized.find("\"kind\":\"solve\"") != std::string::npos);
        REQUIRE(serialized.find("\"conflict_limit\":3") != std::string::npos);
        REQUIRE(serialized.find("\"decision_limit\":7") != std::string::npos);
        REQUIRE(serialized.find("\"kind\":\"set_option\"") != std::string::npos);
        REQUIRE(serialized.find("\"kind\":\"value_of\"") != std::string::npos);
        REQUIRE(serialized.find("\"kind\":\"failed\"") != std::string::npos);
        const auto parsed = incremental_replay_trace::parse_json_lines(serialized);
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->seed == trace.seed);
        REQUIRE(parsed->variable_count == trace.variable_count);
        REQUIRE(parsed->operations.size() == trace.operations.size());
        REQUIRE(parsed->serialize_json_lines() == serialized);
    }

    TEST_CASE("incremental replay executor applies one trace to one solver session", "[sat]")
    {
        using test_support::execute_incremental_replay;
        using test_support::incremental_replay_operation;
        using test_support::incremental_replay_trace;
        using test_support::replay_operation_kind;

        incremental_replay_trace trace;
        trace.variable_count = 2u;
        trace.operations = {
            incremental_replay_operation {.kind = replay_operation_kind::add_clause, .literals = {literal {variable {1u}, false}}},
            incremental_replay_operation {.kind = replay_operation_kind::solve},
            incremental_replay_operation {.kind = replay_operation_kind::assume, .literals = {literal {variable {1u}, true}}},
            incremental_replay_operation {.kind = replay_operation_kind::solve},
            incremental_replay_operation {.kind = replay_operation_kind::failed, .literal_operand = literal {variable {1u}, true}},
            incremental_replay_operation {.kind = replay_operation_kind::release_assumptions},
            incremental_replay_operation {.kind = replay_operation_kind::solve},
            incremental_replay_operation {.kind = replay_operation_kind::value_of, .variable_operand = variable {1u}},
            incremental_replay_operation {.kind = replay_operation_kind::reset_session},
        };

        solver solver;
        const auto outcome = execute_incremental_replay(trace, solver);
        REQUIRE(outcome.solve_statuses.size() == 3u);
        REQUIRE(outcome.solve_statuses[0u] == solve_result::status::satisfiable);
        REQUIRE(outcome.solve_statuses[1u] == solve_result::status::unsatisfiable);
        REQUIRE(outcome.solve_statuses[2u] == solve_result::status::satisfiable);
        REQUIRE(outcome.failed_results.size() == 1u);
        REQUIRE(outcome.failed_results[0u]);
        REQUIRE(outcome.value_results.size() == 1u);
        REQUIRE(outcome.value_results[0u].has_value());
        REQUIRE(*outcome.value_results[0u]);
        REQUIRE(solver.current_state() == solver_state_machine::state::configuring);
    }

    TEST_CASE("serialized incremental replay round-trips into the executor", "[sat]")
    {
        test_support::incremental_replay_trace trace;
        trace.seed = 7u;
        trace.variable_count = 1u;
        trace.operations = {
            {.kind = test_support::replay_operation_kind::add_clause, .literals = {literal {variable {1u}, false}}},
            {.kind = test_support::replay_operation_kind::solve},
        };
        const auto parsed = test_support::incremental_replay_trace::parse_json_lines(trace.serialize_json_lines());
        REQUIRE(parsed.has_value());
        solver solver;
        const auto outcome = test_support::execute_incremental_replay(parsed.value(), solver);
        REQUIRE(outcome.solve_statuses.size() == 1u);
        REQUIRE(outcome.solve_statuses.front() == solve_result::status::satisfiable);
    }
}
