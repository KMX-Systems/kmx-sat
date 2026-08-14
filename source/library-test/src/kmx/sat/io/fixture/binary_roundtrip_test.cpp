#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>

#include <kmx/sat/cdcl/external_frontend.hpp>
#include <kmx/sat/io/file_source.hpp>
#include <kmx/sat/io/fixture/binary/reader.hpp>
#include <kmx/sat/io/fixture/binary/writer.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/solve_request.hpp>
#include <kmx/sat/solver.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::io::fixture
{

    TEST_CASE("binary roundtrip", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::io;
        using namespace kmx::sat::io::fixture;

        const auto temp_dir = std::filesystem::temp_directory_path();

        {
            fixture::binary::writer writer;
            writer.begin_fixture(schema::payload_kind::cnf_fixture);
            const std::array<literal, 2> clause_1 {literal {variable {1}, false}, literal {variable {2}, true}};
            const std::array<literal, 1> clause_2 {literal {variable {3}, false}};
            writer.append_clause(clause_1);
            writer.append_clause(clause_2);
            writer.write_checksum();
            writer.finalize_fixture();

            const auto cnf_path = temp_dir / "kmx_fixture_cnf.satb";
            {
                std::ofstream out {cnf_path, std::ios::binary};
                out << writer.serialized_fixture();
            }

            file_source source;
            REQUIRE(source.open(cnf_path.string()));

            fixture::binary::reader reader;
            REQUIRE(reader.open_fixture(source));
            REQUIRE(reader.read_header());
            REQUIRE(reader.validate_header());
            REQUIRE(reader.load_payload());
            REQUIRE(reader.validate_payload());
            REQUIRE(reader.clauses().size() == 2);
            REQUIRE(reader.clauses()[0].size() == 2);
            REQUIRE(reader.clauses()[1].size() == 1);
            source.close();
            std::filesystem::remove(cnf_path);
        }

        {
            fixture::binary::writer writer;
            writer.begin_fixture(schema::payload_kind::solve_request_fixture);
            solve_request request {};
            request.assumptions = {literal {variable {4}, false}, literal {variable {5}, true}};
            request.conflict_limit = 11u;
            request.decision_limit = 22u;
            request.enabled_pass_mask = 7u;
            request.strict_mode = true;
            writer.set_limits_payload(request);
            writer.write_checksum();
            writer.finalize_fixture();

            const auto request_path = temp_dir / "kmx_fixture_request.satb";
            {
                std::ofstream out {request_path, std::ios::binary};
                out << writer.serialized_fixture();
            }

            file_source source;
            REQUIRE(source.open(request_path.string()));

            fixture::binary::reader reader;
            REQUIRE(reader.open_fixture(source));
            REQUIRE(reader.read_header());
            REQUIRE(reader.validate_header());
            REQUIRE(reader.load_payload());
            REQUIRE(reader.validate_payload());
            REQUIRE(reader.assumptions().size() == 2);
            REQUIRE(reader.request_payload().conflict_limit == 11u);
            REQUIRE(reader.request_payload().decision_limit == 22u);
            REQUIRE(reader.request_payload().enabled_pass_mask == 7u);
            REQUIRE(reader.request_payload().strict_mode);

            cdcl::external_frontend frontend;
            reader.materialize_fixture_into_frontend(frontend);
            REQUIRE(frontend.assumptions().size() == 2u);
            REQUIRE(frontend.prepared_request().assumptions.size() == 2u);
            REQUIRE(frontend.prepared_request().conflict_limit == 11u);
            REQUIRE(frontend.prepared_request().decision_limit == 22u);
            source.close();
            std::filesystem::remove(request_path);
        }

        {
            fixture::binary::writer writer;
            writer.begin_fixture(schema::payload_kind::cnf_fixture);
            const std::array<literal, 2> clause_1 {literal {variable {7}, false}, literal {variable {8}, true}};
            const std::array<literal, 1> clause_2 {literal {variable {9}, false}};
            writer.append_clause(clause_1);
            writer.append_clause(clause_2);
            writer.write_checksum();
            writer.finalize_fixture();

            const auto cnf_path = temp_dir / "kmx_fixture_cnf_materialized.satb";
            {
                std::ofstream out {cnf_path, std::ios::binary};
                out << writer.serialized_fixture();
            }

            file_source source;
            REQUIRE(source.open(cnf_path.string()));

            fixture::binary::reader reader;
            REQUIRE(reader.open_fixture(source));
            REQUIRE(reader.read_header());
            REQUIRE(reader.validate_header());
            REQUIRE(reader.load_payload());
            REQUIRE(reader.validate_payload());

            cdcl::variable_mapper mapper;
            cdcl::external_frontend frontend {mapper};
            reader.materialize_fixture_into_frontend(frontend);
            REQUIRE(frontend.clauses().size() == 2u);
            REQUIRE(frontend.clauses()[0].size() == 2u);
            REQUIRE(frontend.clauses()[1].size() == 1u);
            REQUIRE(frontend.clauses()[0][0] == mapper.to_internal_literal(clause_1[0]));
            REQUIRE(frontend.clauses()[0][1] == mapper.to_internal_literal(clause_1[1]));
            REQUIRE(frontend.clauses()[1][0] == mapper.to_internal_literal(clause_2[0]));
            REQUIRE(frontend.assumptions().empty());
            source.close();
            std::filesystem::remove(cnf_path);
        }

        // removed std::cout: "binary fixture roundtrip test passed\n";
    }

    TEST_CASE("binary reader rejects mismatched payload counts", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::io;
        using namespace kmx::sat::io::fixture;

        const auto temp_dir = std::filesystem::temp_directory_path();
        const auto malformed_path = temp_dir / "kmx_fixture_malformed.satb";
        const std::string malformed_fixture {"SATB 1 0 0 3 2 0 0 0 0 0 0\n"
                                             "c\n"
                                             "1 0\n"};

        {
            std::ofstream out {malformed_path, std::ios::binary};
            out << malformed_fixture;
        }

        file_source source;
        REQUIRE(source.open(malformed_path.string()));

        fixture::binary::reader reader;
        REQUIRE(reader.open_fixture(source));
        REQUIRE(reader.read_header());
        REQUIRE(reader.validate_header());
        REQUIRE_FALSE(reader.load_payload());

        source.close();
        std::filesystem::remove(malformed_path);
    }

    TEST_CASE("binary reader rejects malformed clause payload", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::io;
        using namespace kmx::sat::io::fixture;

        const auto temp_dir = std::filesystem::temp_directory_path();
        const auto malformed_path = temp_dir / "kmx_fixture_malformed_clause.satb";
        const std::string malformed_fixture {"SATB 1 0 0 3 1 0 0 0 0 0 0\n"
                                             "c\n"
                                             "0\n"};

        {
            std::ofstream out {malformed_path, std::ios::binary};
            out << malformed_fixture;
        }

        file_source source;
        REQUIRE(source.open(malformed_path.string()));

        fixture::binary::reader reader;
        REQUIRE(reader.open_fixture(source));
        REQUIRE(reader.read_header());
        REQUIRE(reader.validate_header());
        REQUIRE_FALSE(reader.load_payload());

        source.close();
        std::filesystem::remove(malformed_path);
    }

    TEST_CASE("binary reader rejects schema checksum truncation and domain violations", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::io;
        using namespace kmx::sat::io::fixture;

        const auto temp_dir = std::filesystem::temp_directory_path();
        const auto check_fixture = [&](const std::string& name, const std::string& contents, const bool header_valid,
                                       const bool payload_valid)
        {
            const auto path = temp_dir / name;
            {
                std::ofstream out {path, std::ios::binary};
                out << contents;
            }

            file_source source;
            REQUIRE(source.open(path.string()));
            fixture::binary::reader reader;
            REQUIRE(reader.open_fixture(source));
            REQUIRE(reader.read_header());
            REQUIRE(reader.validate_header() == header_valid);
            if (header_valid)
            {
                REQUIRE(reader.load_payload());
                REQUIRE(reader.validate_payload() == payload_valid);
            }
            source.close();
            std::filesystem::remove(path);
        };

        check_fixture("kmx_fixture_bad_version.satb", "SATB 2 0 0 3 1 0 0 0 0 0 0\nc\n1 0\n", false, false);
        check_fixture("kmx_fixture_bad_flags.satb", "SATB 1 0 1 3 1 0 0 0 0 0 0\nc\n1 0\n", false, false);
        check_fixture("kmx_fixture_bad_checksum.satb", "SATB 1 0 0 3 1 0 0 0 0 0 0\nc\n1 0\n", true, false);
        check_fixture("kmx_fixture_bad_domain.satb", "SATB 1 0 0 1 1 0 0 0 0 0 0\nc\n2 0\n", true, false);

        const auto truncated_path = temp_dir / "kmx_fixture_truncated_payload.satb";
        {
            std::ofstream out {truncated_path, std::ios::binary};
            out << "SATB 1 0 0 3 2 0 0 0 0 0 0\nc\n1 0\n";
        }
        file_source truncated_source;
        REQUIRE(truncated_source.open(truncated_path.string()));
        fixture::binary::reader truncated_reader;
        REQUIRE(truncated_reader.open_fixture(truncated_source));
        REQUIRE(truncated_reader.read_header());
        REQUIRE(truncated_reader.validate_header());
        REQUIRE_FALSE(truncated_reader.load_payload());
        truncated_source.close();
        std::filesystem::remove(truncated_path);
    }

    TEST_CASE("binary fixture materialization matches DIMACS solver outcomes", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::io;
        using namespace kmx::sat::io::fixture;

        const auto temp_path = std::filesystem::temp_directory_path() / "kmx_fixture_equivalence.satb";
        fixture::binary::writer writer;
        writer.begin_fixture(schema::payload_kind::cnf_fixture);
        const std::array<literal, 1> unit {literal {variable {1u}, false}};
        writer.append_clause(unit);
        writer.write_checksum();
        writer.finalize_fixture();
        {
            std::ofstream out {temp_path, std::ios::binary};
            out << writer.serialized_fixture();
        }

        file_source source;
        REQUIRE(source.open(temp_path.string()));
        fixture::binary::reader reader;
        REQUIRE(reader.open_fixture(source));
        REQUIRE(reader.read_header());
        REQUIRE(reader.validate_header());
        REQUIRE(reader.load_payload());
        REQUIRE(reader.validate_payload());

        cdcl::external_frontend frontend;
        reader.materialize_fixture_into_frontend(frontend);
        solver binary_solver;
        for (const auto& clause: frontend.clauses())
        {
            binary_solver.add_clause(clause);
        }
        binary_solver.assume(literal {variable {1u}, true});
        const auto binary_result = binary_solver.solve(solve_request {});
        REQUIRE(binary_result.status_of() == solve_result::status::unsatisfiable);
        REQUIRE(binary_solver.failed(literal {variable {1u}, true}));

        solver dimacs_solver;
        dimacs_solver.add_clause(unit);
        dimacs_solver.assume(literal {variable {1u}, true});
        const auto dimacs_result = dimacs_solver.solve(solve_request {});
        REQUIRE(dimacs_result.status_of() == binary_result.status_of());
        REQUIRE(dimacs_solver.failed(literal {variable {1u}, true}) == binary_solver.failed(literal {variable {1u}, true}));

        source.close();
        std::filesystem::remove(temp_path);
    }

    TEST_CASE("invalid binary fixture materialization leaves frontend unchanged", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::io;
        using namespace kmx::sat::io::fixture;

        const auto temp_path = std::filesystem::temp_directory_path() / "kmx_fixture_rollback.satb";
        const std::string malformed_fixture {"SATB 1 0 0 3 2 0 0 0 0 0 0\n"
                                             "c\n"
                                             "1 0\n"};
        {
            std::ofstream out {temp_path, std::ios::binary};
            out << malformed_fixture;
        }

        file_source source;
        REQUIRE(source.open(temp_path.string()));
        fixture::binary::reader reader;
        REQUIRE(reader.open_fixture(source));
        REQUIRE(reader.read_header());
        REQUIRE(reader.validate_header());
        REQUIRE_FALSE(reader.load_payload());

        cdcl::external_frontend frontend;
        const std::array<literal, 1> sentinel {literal {variable {9u}, false}};
        frontend.push_clause(sentinel);
        reader.materialize_fixture_into_frontend(frontend);
        REQUIRE(frontend.clauses().size() == 1u);
        REQUIRE(frontend.clauses().front().front() == sentinel.front());

        source.close();
        std::filesystem::remove(temp_path);
    }

} // namespace
