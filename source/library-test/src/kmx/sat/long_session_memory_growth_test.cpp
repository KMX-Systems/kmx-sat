#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>

#include <kmx/sat/literal.hpp>
#include <kmx/sat/solve_request.hpp>
#include <kmx/sat/solver.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat
{
    static std::size_t resident_set_kb() noexcept
    {
        std::ifstream status {"/proc/self/status"};
        std::string name;
        std::size_t value {};
        std::string unit;
        while (status >> name >> value >> unit)
            if (name == "VmRSS:")
                return value;
        return 0u;
    }

    TEST_CASE("long incremental session keeps memory and telemetry bounded", "[sat]")
    {
        const std::array<literal, 1u> unit_clause {literal {variable {1u}, false}};
        solver solver;
        solver.set_option(option_id::chb_enabled, 1L);
        solver.set_option(option_id::reduction_fraction_percent, 30L);
        solver.add_clause(unit_clause);

        const auto initial_rss_kb = resident_set_kb();
        auto peak_rss_kb = initial_rss_kb;
        const char* report_path = std::getenv("KMX_SAT_MEMORY_REPORT");
        std::ofstream report;
        if ((report_path != nullptr) && (*report_path != '\0'))
        {
            report.open(report_path, std::ios::out | std::ios::trunc);
            report << "{\"schema\":1,\"kind\":\"header\",\"episodes\":1000}\n";
        }
        const auto emit_sample = [&](const std::uint32_t episode, const char* operation, const char* status)
        {
            if (!report.is_open())
                return;
            const auto statistics = solver.statistics();
            report << "{\"kind\":\"sample\",\"episode\":" << episode << ",\"operation\":\"" << operation << "\",\"status\":\"" << status
                   << "\",\"rss_kb\":" << resident_set_kb() << ",\"conflicts\":" << statistics.conflicts
                   << ",\"decisions\":" << statistics.decisions << ",\"restarts\":" << statistics.restarts
                   << ",\"learned_clauses\":" << statistics.learned_clauses << ",\"reduction_passes\":" << statistics.reduction_passes
                   << ",\"reduced_clauses\":" << statistics.reduced_clauses << ",\"deleted_clauses\":" << statistics.deleted_clauses
                   << ",\"proof_buffered_payload_bytes\":" << solver.proof_buffered_payload_bytes()
                   << ",\"cold_footprint_bytes\":" << solver.cold_footprint_bytes()
                   << ",\"report_buffer_size\":" << solver.statistics_report_emission_count()
                   << ",\"persisted_configuration\":" << (solver.has_persisted_configuration() ? "true" : "false") << "}\n";
        };
        for (std::uint32_t episode {}; episode < 1000u; ++episode)
        {
            solver.assume(literal {variable {1u}, true});
            const auto unsat_result = solver.solve(solve_request {});
            REQUIRE(unsat_result.status_of() == solve_result::status::unsatisfiable);
            REQUIRE(solver.failed(literal {variable {1u}, true}));
            emit_sample(episode, "assumption_unsat", "UNSATISFIABLE");
            solver.release_incremental_assumptions();

            const auto sat_result = solver.solve(solve_request {});
            REQUIRE(sat_result.status_of() == solve_result::status::satisfiable);
            REQUIRE(solver.value_of(variable {1u}).value_or(false));
            emit_sample(episode, "release_sat", "SATISFIABLE");
            REQUIRE(solver.statistics_report_emission_count() <= 64u);
            peak_rss_kb = std::max(peak_rss_kb, resident_set_kb());

            if ((episode + 1u) % 32u == 0u)
            {
                solver.reset_session();
                REQUIRE(solver.current_state() == solver_state_machine::state::configuring);
                solver.set_option(option_id::chb_enabled, 1L);
                solver.set_option(option_id::reduction_fraction_percent, 30L);
                solver.add_clause(unit_clause);
                emit_sample(episode, "reset", "RESET");
            }
        }

        REQUIRE(peak_rss_kb >= initial_rss_kb);
        REQUIRE(peak_rss_kb <= initial_rss_kb + 64u * 1024u);
    }
}
