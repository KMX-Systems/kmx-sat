/// @file cli/kmx-sat-main.cpp
/// @brief Command-line SAT solver wrapper for DIMACS CNF input.
/// @details Reads standard DIMACS format, invokes solver, outputs DIMACS standard status.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <kmx/sat/literal.hpp>
#include <kmx/sat/solve_request.hpp>
#include <kmx/sat/solver.hpp>
#include <kmx/sat/variable.hpp>

namespace fs = std::filesystem;

static int read_dimacs_cnf(const fs::path& cnf_file, kmx::sat::solver& solver) noexcept
{
    std::ifstream stream(cnf_file);
    if (!stream.is_open())
    {
        std::cerr << "Error: cannot open file " << cnf_file << "\n";
        return 1;
    }

    std::string line;
    int var_count = 0;
    int clause_count = 0;
    int line_number = 0;

    while (std::getline(stream, line))
    {
        ++line_number;
        if (line.empty() || line[0] == 'c')
            continue;

        if (line[0] == 'p')
        {
            if (std::sscanf(line.c_str(), "p cnf %d %d", &var_count, &clause_count) != 2)
            {
                std::cerr << "Error: invalid problem line (expected 'p cnf <variables> <clauses>')\n";
                return 1;
            }
            solver.reserve(static_cast<kmx::sat::variable::index_t>(var_count));
            continue;
        }

        std::istringstream clause_stream(line);
        int lit_value = 0;
        while (clause_stream >> lit_value && lit_value != 0)
        {
            kmx::sat::variable var(static_cast<kmx::sat::variable::index_t>(std::abs(lit_value)));
            kmx::sat::literal lit {var, lit_value < 0};
            solver.add_literal(lit);
        }
        if (lit_value != 0)
        {
            std::cerr << "Error: clause at line " << line_number << " does not end with 0\n";
            return 1;
        }
        solver.add_literal(kmx::sat::literal(0));
    }

    return 0;
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "usage: " << argv[0] << " <cnf-file> [--assume <lit>]... [--decision-limit <n>]"
                  << " [--conflict-limit <n>] [--restart-interval <n>]" << " [--decision-restart-interval <n>] [--reduction-interval <n>]"
                  << " [--reduction-fraction-percent <n>]\n";
        return 1;
    }

    const fs::path cnf_file = argv[1];
    if (!fs::is_regular_file(cnf_file))
    {
        std::cerr << "Error: not a regular file: " << cnf_file << "\n";
        return 1;
    }

    kmx::sat::solver solver;
    const int read_status = read_dimacs_cnf(cnf_file, solver);
    if (read_status != 0)
        return read_status;

    kmx::sat::solve_request request;
    for (int index = 2; index < argc; ++index)
    {
        const std::string option = argv[index];
        if (option == "--assume" && index + 1 < argc)
        {
            const int value = std::atoi(argv[++index]);
            if (value == 0)
            {
                std::cerr << "Error: assumption literal must be nonzero\n";
                return 1;
            }
            request.assumptions.emplace_back(kmx::sat::variable(static_cast<kmx::sat::variable::index_t>(std::abs(value))), value < 0);
        }
        else if (option == "--decision-limit" && index + 1 < argc)
            request.decision_limit = std::strtoull(argv[++index], nullptr, 10);
        else if (option == "--conflict-limit" && index + 1 < argc)
            request.conflict_limit = std::strtoull(argv[++index], nullptr, 10);
        else if (option == "--restart-interval" && index + 1 < argc)
            solver.set_option("restart_interval", std::strtoull(argv[++index], nullptr, 10));
        else if (option == "--decision-restart-interval" && index + 1 < argc)
            solver.set_option("decision_restart_interval", std::strtoull(argv[++index], nullptr, 10));
        else if (option == "--reduction-interval" && index + 1 < argc)
            solver.set_option("reduction_interval", std::strtoull(argv[++index], nullptr, 10));
        else if (option == "--reduction-fraction-percent" && index + 1 < argc)
            solver.set_option("reduction_fraction_percent", std::strtoull(argv[++index], nullptr, 10));
        else
        {
            std::cerr << "Error: unknown or incomplete option: " << option << "\n";
            return 1;
        }
    }
    const auto result = solver.solve(request);
    const auto statistics = solver.statistics();
    std::cout << "c kmx-stats conflicts=" << statistics.conflicts << " decisions=" << statistics.decisions
              << " propagations=" << statistics.propagations << " restarts=" << statistics.restarts
              << " learned_clauses=" << statistics.learned_clauses << " learned_clause_glue_total=" << statistics.learned_clause_glue_total
              << " learned_clause_glue_samples=" << statistics.learned_clause_glue_samples
              << " reduction_passes=" << statistics.reduction_passes << " reduced_clauses=" << statistics.reduced_clauses
              << " deleted_clauses=" << statistics.deleted_clauses << " proof_events=" << solver.proof_buffered_event_count()
              << " proof_buffered_payload_bytes=" << solver.proof_buffered_payload_bytes() << "\n";

    switch (result.status_of())
    {
        case kmx::sat::solve_result::status::satisfiable:
            std::cout << "s SATISFIABLE\n";
            return 10;
        case kmx::sat::solve_result::status::unsatisfiable:
            std::cout << "s UNSATISFIABLE\n";
            return 20;
        case kmx::sat::solve_result::status::unknown:
        case kmx::sat::solve_result::status::terminated:
        default:
            std::cout << "s UNKNOWN\n";
            return 0;
    }
}
