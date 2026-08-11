#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/cdcl/compaction_service.hpp>
#include <kmx/sat/cdcl/variable_mapper.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("compaction service rebuilds variable mappings densely", "[sat]")
    {
        variable_mapper mapper;
        const variable first {7u};
        const variable second {5u};

        const variable first_internal = mapper.ensure_external_variable(first);
        const variable second_internal = mapper.ensure_external_variable(second);

        compaction_service service;
        service.attach_mapper(mapper);
        service.build_variable_permutation();
        service.rewrite_external_mapping();

        constexpr std::uint32_t reserved_internal_base = 1u << 30;
        REQUIRE(mapper.to_internal_literal(literal {first, false}).variable_of().index() >= reserved_internal_base);
        REQUIRE(mapper.to_internal_literal(literal {second, false}).variable_of().index() >= reserved_internal_base);
        REQUIRE(mapper.to_external_literal(literal {first_internal, false}).variable_of().index() == first.index());
        REQUIRE(mapper.to_external_literal(literal {second_internal, false}).variable_of().index() == second.index());
    }
}
