#include <catch2/catch_test_macros.hpp>

#include <array>

#include <kmx/sat/cdcl/bank/watch_list.hpp>
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/cdcl/compaction_service.hpp>
#include <kmx/sat/cdcl/variable_mapper.hpp>
#include <kmx/sat/cdcl/watch.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    TEST_CASE("compaction service rebuilds variable mappings densely", "[sat]")
    {
        variable_mapper mapper;
        const variable first {7u};
        const variable second {5u};
        const variable third {9u};

        const variable first_internal = mapper.ensure_external_variable(first);
        const variable second_internal = mapper.ensure_external_variable(second);
        const variable third_internal = mapper.ensure_external_variable(third);

        mapper.mark_eliminated(second_internal);

        clause::database database;
        const std::array<literal, 2> clause_literals {literal {first_internal, false}, literal {third_internal, true}};
        const auto clause_ref = database.add_clause(clause_literals, false);

        bank::watch_list watches;
        const literal old_watched_literal {third_internal, false};
        watches.watch_literal(old_watched_literal, watch {old_watched_literal.negated(), clause_ref});

        compaction_service service;
        service.attach_mapper(mapper);
        service.attach_database(database);
        service.attach_watch_list(watches);
        service.build_variable_permutation();
        service.rewrite_literals();
        service.rewrite_watches();
        service.rewrite_external_mapping();

        constexpr std::uint32_t reserved_internal_base = 1u << 30;
        const auto first_compacted = mapper.to_internal_literal(literal {first, false});
        const auto second_compacted = mapper.to_internal_literal(literal {second, false});
        const auto third_compacted = mapper.to_internal_literal(literal {third, false});

        REQUIRE(service.should_compact() == true);
        REQUIRE(first_compacted.variable_of().index() == reserved_internal_base);
        REQUIRE(second_compacted.variable_of().index() == second_internal.index());
        REQUIRE(third_compacted.variable_of().index() == reserved_internal_base + 1u);
        REQUIRE(mapper.to_external_literal(literal {first_compacted.variable_of(), false}).variable_of().index() == first.index());
        REQUIRE(mapper.to_external_literal(literal {second_internal, false}).variable_of().index() == second.index());

        const auto rewritten_clause_literals = database.storage_of().literals_of(clause_ref);
        REQUIRE(rewritten_clause_literals.size() == 2u);
        REQUIRE(rewritten_clause_literals[0].variable_of().index() == first_compacted.variable_of().index());
        REQUIRE(rewritten_clause_literals[1].variable_of().index() == third_compacted.variable_of().index());

        REQUIRE(watches.size_of(old_watched_literal) == 0u);
        REQUIRE(watches.contains(literal {third_compacted.variable_of(), false},
                                 watch {literal {third_compacted.variable_of(), true}, clause_ref}));
    }
}
