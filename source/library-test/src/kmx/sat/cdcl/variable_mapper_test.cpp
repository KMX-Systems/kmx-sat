#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <kmx/sat/cdcl/variable_mapper.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl {

TEST_CASE("variable mapper", "[sat]")
{
    using namespace kmx::sat;
    using namespace kmx::sat::cdcl;

    variable_mapper mapper;
    const variable external {7u};
    const variable internal = mapper.ensure_external_variable(external);
    REQUIRE(internal.index() > 0u);
    REQUIRE(mapper.to_internal_literal(literal {external, false}).variable_of().index() == internal.index());
    REQUIRE(mapper.to_external_literal(literal {internal, true}).variable_of().index() == external.index());

    mapper.mark_eliminated(internal);
    mapper.mark_inactive(external);
    REQUIRE(mapper.is_eliminated(internal));
    REQUIRE(mapper.is_inactive(external));

    mapper.rebuild_after_compaction();
    REQUIRE(mapper.to_internal_literal(literal {external, true}).variable_of().index() == internal.index());

    const variable aliased_external {1u};
    const variable reserved_internal = mapper.ensure_external_variable(aliased_external);
    constexpr std::uint32_t reserved_internal_base = 1u << 30;
    REQUIRE(reserved_internal.index() != aliased_external.index());
    REQUIRE(reserved_internal.index() >= reserved_internal_base);

    // removed std::cout: "variable mapper test passed\n";
    }

} // namespace
