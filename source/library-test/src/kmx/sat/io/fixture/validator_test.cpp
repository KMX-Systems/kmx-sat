#include <catch2/catch_test_macros.hpp>

#include <array>

#include <kmx/sat/io/fixture/schema.hpp>
#include <kmx/sat/io/fixture/validator.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/solve_request.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::io::fixture
{

    TEST_CASE("validator", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::io::fixture;

        const literal x_pos {variable {1u}, false};
        const literal y_neg {variable {2u}, true};
        const literal z_pos {variable {3u}, false};

        schema cnf_schema;
        cnf_schema.set_payload_kind(schema::payload_kind::cnf_fixture);

        validator cnf_validator;
        clause_list_t clauses {{x_pos, y_neg}, {z_pos}};
        cnf_validator.set_declared_variable_count(3u);
        cnf_validator.set_clauses(clauses);
        cnf_validator.set_header("SATB", cnf_schema.current_version(), 0u, cnf_schema);
        cnf_validator.set_header("SATB", cnf_schema.current_version(), cnf_validator.payload_checksum(), cnf_schema);

        REQUIRE(cnf_validator.validate_magic());
        REQUIRE(cnf_validator.validate_version());
        REQUIRE(cnf_validator.validate_checksum());
        REQUIRE(cnf_validator.validate_literal_domain());
        REQUIRE(cnf_validator.validate_clause_shapes());
        REQUIRE(cnf_validator.validate_limits_payload());

        validator bad_magic = cnf_validator;
        bad_magic.set_header("BAD!", cnf_schema.current_version(), bad_magic.payload_checksum(), cnf_schema);
        REQUIRE(!bad_magic.validate_magic());

        validator bad_version = cnf_validator;
        bad_version.set_header("SATB", 99u, bad_version.payload_checksum(), cnf_schema);
        REQUIRE(!bad_version.validate_version());

        validator bad_checksum = cnf_validator;
        bad_checksum.set_header("SATB", cnf_schema.current_version(), bad_checksum.payload_checksum() + 1u, cnf_schema);
        REQUIRE(!bad_checksum.validate_checksum());

        validator bad_domain;
        clause_list_t out_of_domain_clauses {{literal {variable {4u}, false}}};
        bad_domain.set_declared_variable_count(3u);
        bad_domain.set_clauses(out_of_domain_clauses);
        bad_domain.set_header("SATB", cnf_schema.current_version(), 0u, cnf_schema);
        bad_domain.set_header("SATB", cnf_schema.current_version(), bad_domain.payload_checksum(), cnf_schema);
        REQUIRE(!bad_domain.validate_literal_domain());

        validator bad_shape;
        clause_list_t malformed_clauses {{}, {x_pos}};
        bad_shape.set_declared_variable_count(3u);
        bad_shape.set_clauses(malformed_clauses);
        bad_shape.set_header("SATB", cnf_schema.current_version(), 0u, cnf_schema);
        bad_shape.set_header("SATB", cnf_schema.current_version(), bad_shape.payload_checksum(), cnf_schema);
        REQUIRE(!bad_shape.validate_clause_shapes());

        schema request_schema;
        request_schema.set_payload_kind(schema::payload_kind::solve_request_fixture);

        validator request_validator;
        solve_request request {};
        request.assumptions = {x_pos, y_neg};
        request.conflict_limit = 10u;
        request.decision_limit = 20u;
        request.strict_mode = true;
        request_validator.set_declared_variable_count(3u);
        request_validator.set_limits_payload(request);
        request_validator.set_header("SATB", request_schema.current_version(), 0u, request_schema);
        request_validator.set_header("SATB", request_schema.current_version(), request_validator.payload_checksum(), request_schema);
        REQUIRE(request_validator.validate_limits_payload());
        REQUIRE(request_validator.validate_literal_domain());

        solve_request bad_request = request;
        bad_request.assumptions = {literal {}};
        request_validator.set_limits_payload(bad_request);
        request_validator.set_header("SATB", request_schema.current_version(), request_validator.payload_checksum(), request_schema);
        REQUIRE(!request_validator.validate_limits_payload());

        // removed std::cout: "fixture validator test passed\n";
    }

} // namespace
