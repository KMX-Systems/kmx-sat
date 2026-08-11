#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>
#include <vector>

#include <kmx/sat/cdcl/clause/learner.hpp>
#include <kmx/sat/cdcl/conflict_analyzer.hpp>
#include <kmx/sat/cdcl/propagator.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl {

TEST_CASE("cdcl component state", "[sat]")
{
    using namespace kmx::sat;
    using namespace kmx::sat::cdcl;

    propagator unit_propagator;
    const clause::ref_t clause_a {11};
    const clause::ref_t clause_b {22};
    const clause::ref_t clause_c {33};

    unit_propagator.attach_clause(clause_a);
    unit_propagator.watch_clause(clause_a);
    unit_propagator.attach_clause(clause_b);
    REQUIRE(unit_propagator.watched_clause_count() == 2);

    unit_propagator.detach_clause(clause_a);
    REQUIRE(unit_propagator.watched_clause_count() == 1);

    unit_propagator.set_pending_assumption_count(1);
    unit_propagator.stage_conflict(clause_b);
    unit_propagator.stage_conflict(clause_c);

    const auto assumption_conflict = unit_propagator.propagate_assumptions();
    REQUIRE(assumption_conflict.valid());
    REQUIRE(assumption_conflict.offset() == clause_b.offset());

    const auto core_conflict = unit_propagator.propagate();
    REQUIRE(core_conflict.valid());
    REQUIRE(core_conflict.offset() == clause_c.offset());

    REQUIRE(!unit_propagator.propagate_beyond_conflict().valid());
    REQUIRE(unit_propagator.assumption_propagation_call_count() == 1);
    REQUIRE(unit_propagator.propagation_call_count() == 1);
    REQUIRE(unit_propagator.beyond_conflict_propagation_call_count() == 1);

    conflict_analyzer analyzer;
    const std::vector<literal> conflict_clause {
        literal {variable {1}, false},
        literal {variable {2}, true},
        literal {variable {3}, false},
        literal {variable {2}, true}
    };
    analyzer.seed_conflict_clause(std::span<const literal> {conflict_clause});
    analyzer.set_decision_level(variable {1}, 1);
    analyzer.set_decision_level(variable {2}, 4);
    analyzer.set_decision_level(variable {3}, 2);

    analyzer.analyze();

    const auto learned_clause = analyzer.learned_clause();
    REQUIRE(learned_clause.size() == 3);
    REQUIRE(analyzer.derive_first_uip().raw() == conflict_clause.front().raw());
    REQUIRE(analyzer.compute_backjump_level() == 2);

    const auto bump_candidates = analyzer.collect_bump_candidates();
    REQUIRE(bump_candidates.size() == 3);
    REQUIRE(analyzer.bump_candidate_count() == 3u);

    analyzer.build_resolution_chain();
    REQUIRE(analyzer.resolution_chain_step_count() == 2);
    REQUIRE(analyzer.has_learned_clause());

    clause::learner learned_clause_registry;
    REQUIRE(!learned_clause_registry.learn_clause(std::span<const literal> {}).valid());

    const std::array<literal, 1> unit_clause {literal {variable {4}, false}};
    const auto unit_ref = learned_clause_registry.learn_clause(std::span<const literal> {unit_clause});
    REQUIRE(unit_ref.valid());
    REQUIRE(learned_clause_registry.learned_clause_count() == 1);

    const std::array<literal, 2> binary_clause {
        literal {variable {5}, false},
        literal {variable {6}, true}
    };
    const auto binary_ref = learned_clause_registry.learn_clause(std::span<const literal> {binary_clause});
    REQUIRE(binary_ref.valid());
    REQUIRE(binary_ref.offset() > unit_ref.offset());
    REQUIRE(learned_clause_registry.learned_clause_count() == 2);

    const std::array<literal, 3> large_clause {
        literal {variable {7}, false},
        literal {variable {8}, true},
        literal {variable {9}, false}
    };
    const auto large_ref = learned_clause_registry.learn_clause(std::span<const literal> {large_clause});
    REQUIRE(large_ref.valid());
    REQUIRE(large_ref.offset() > binary_ref.offset());
    REQUIRE(learned_clause_registry.learned_clause_count() == 3);

    const auto latest_clause = learned_clause_registry.last_learned_clause();
    REQUIRE(latest_clause.size() == 3);
    REQUIRE(latest_clause[2].raw() == large_clause[2].raw());
    REQUIRE(learned_clause_registry.has_pending_clause());

    learned_clause_registry.assign_asserting_literal(unit_clause[0]);
    REQUIRE(learned_clause_registry.last_asserting_literal().raw() == unit_clause[0].raw());

    // removed std::cout: "cdcl component state test passed\n";
    }

} // namespace
