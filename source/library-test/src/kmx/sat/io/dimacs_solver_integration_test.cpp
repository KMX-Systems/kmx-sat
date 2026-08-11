#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <fstream>
#include <span>
#include <string>

#include <kmx/sat/io/dimacs_parser.hpp>
#include <kmx/sat/io/file_source.hpp>
#include <kmx/sat/solver.hpp>

namespace kmx::sat::io {

TEST_CASE("dimacs solver integration", "[sat]")
{
    using namespace kmx::sat;
    using namespace kmx::sat::io;

    const std::string sat_path = "source/library-test/tmp_dimacs_sat.cnf";
    {
        std::ofstream out {sat_path};
        out << "c simple sat instance\n";
        out << "p cnf 2 2\n";
        out << "1 0\n";
        out << "-1 2 0\n";
    }

    file_source sat_source;
    REQUIRE(sat_source.open(sat_path));

    dimacs_parser sat_parser;
    REQUIRE(sat_parser.parse(sat_source));
    REQUIRE(sat_parser.declared_variable_count() == 2);
    REQUIRE(sat_parser.declared_clause_count() == 2);

    solver sat_solver;
    for (const auto& clause : sat_parser.clauses())
    {
        sat_solver.add_clause(std::span<const literal> {clause});
    }
    const auto sat_result = sat_solver.solve(solve_request {});
    REQUIRE(sat_result.status_of() == solve_result::status::satisfiable);
    sat_source.close();

    const std::string unsat_path = "source/library-test/tmp_dimacs_unsat.cnf";
    {
        std::ofstream out {unsat_path};
        out << "c simple unsat instance\n";
        out << "p cnf 1 2\n";
        out << "1 0\n";
        out << "-1 0\n";
    }

    file_source unsat_source;
    REQUIRE(unsat_source.open(unsat_path));

    dimacs_parser unsat_parser;
    REQUIRE(unsat_parser.parse(unsat_source));

    solver unsat_solver;
    for (const auto& clause : unsat_parser.clauses())
    {
        unsat_solver.add_clause(std::span<const literal> {clause});
    }
    const auto unsat_result = unsat_solver.solve(solve_request {});
    REQUIRE(unsat_result.status_of() == solve_result::status::unsatisfiable);
    unsat_source.close();

    std::remove(sat_path.c_str());
    std::remove(unsat_path.c_str());

    // removed std::cout: "dimacs solver integration test passed\n";
    }

} // namespace
