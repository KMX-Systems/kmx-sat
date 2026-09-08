#include <catch2/catch_test_macros.hpp>

#include <array>

#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/compaction_service.hpp>
#include <kmx/sat/cdcl/store/assignment.hpp>
#include <kmx/sat/cdcl/variable_mapper.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("compaction service rewrites assignment reasons after clause relocation", "[sat]")
    {
        variable_mapper mapper;
        const auto internal_v1 = mapper.ensure_external_variable(variable {1u});
        const auto internal_v2 = mapper.ensure_external_variable(variable {2u});

        clause::database database;
        const auto reason_ref =
            database.add_clause(std::array<literal, 2u> {literal {internal_v1, false}, literal {internal_v2, true}}, false);

        store::assignment assignment;
        assignment.set_current_level(1u);
        assignment.set_current_trail_position(0u);
        assignment.assign(literal {internal_v1, false}, reason_ref);

        compaction_service service;
        service.attach_mapper(mapper);
        service.attach_database(database);
        service.attach_assignment_store(assignment);

        service.build_variable_permutation();
        service.rewrite_literals();
        service.rewrite_reasons();
        service.rewrite_external_mapping();

        const auto relocated_ref = database.storage_of().resolve_ref(reason_ref);
        REQUIRE(relocated_ref.valid());
        REQUIRE(relocated_ref != reason_ref);
        REQUIRE(assignment.reason_of(internal_v1) == relocated_ref);
    }

    TEST_CASE("assignment store handles reserved high internal variables sparsely", "[sat]")
    {
        store::assignment assignment;
        const variable high_variable {1u << 30};
        const clause::ref_t reason {42u};
        assignment.set_current_level(3u);
        assignment.set_current_trail_position(7u);
        assignment.assign(literal {high_variable, false}, reason);

        REQUIRE(assignment.value_of(high_variable).has_value());
        REQUIRE(*assignment.value_of(high_variable));
        REQUIRE(assignment.reason_of(high_variable) == reason);
        REQUIRE(assignment.level_of(high_variable) == 3u);
        REQUIRE(assignment.trail_position_of(high_variable) == 7u);

        assignment.mark_analyzed(high_variable);
        REQUIRE(assignment.analysis_seen(high_variable));
        assignment.unassign_from(high_variable);
        REQUIRE_FALSE(assignment.value_of(high_variable).has_value());
        REQUIRE(assignment.reason_of(high_variable).invalid());
    }
}