#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>
#include <vector>

#include <kmx/sat/cdcl/clause/learner.hpp>
#include <kmx/sat/cdcl/conflict_analyzer.hpp>
#include <kmx/sat/cdcl/propagator.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{

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

        conflict_analyzer empty_analyzer;
        empty_analyzer.analyze();
        REQUIRE_FALSE(empty_analyzer.has_learned_clause());
        REQUIRE(empty_analyzer.learned_clause().empty());
        REQUIRE(empty_analyzer.resolution_chain_step_count() == 0u);
        REQUIRE(empty_analyzer.bump_candidate_count() == 0u);

        conflict_analyzer analyzer;
        const std::vector<literal> conflict_clause {literal {variable {1}, false}, literal {variable {2}, true},
                                                    literal {variable {3}, false}, literal {variable {2}, true}};
        analyzer.seed_conflict_clause(std::span<const literal> {conflict_clause});
        analyzer.set_decision_level(variable {1}, 1);
        analyzer.set_decision_level(variable {2}, 4);
        analyzer.set_decision_level(variable {3}, 2);

        analyzer.analyze();

        const auto learned_clause = analyzer.learned_clause();
        REQUIRE(learned_clause.size() == 3);
        REQUIRE(learned_clause[0].variable_of().index() == 2u);
        REQUIRE(analyzer.derive_first_uip().raw() == literal {variable {2}, true}.raw());
        REQUIRE(analyzer.compute_backjump_level() == 2);

        const auto bump_candidates = analyzer.collect_bump_candidates();
        REQUIRE(bump_candidates.size() == 3);
        REQUIRE(analyzer.bump_candidate_count() == 3u);

        analyzer.build_resolution_chain();
        REQUIRE(analyzer.resolution_chain_step_count() == 2);
        const auto chain_literals = analyzer.resolution_chain_literals();
        REQUIRE(chain_literals.size() == 2u);
        REQUIRE(chain_literals[0].raw() == learned_clause[1].raw());
        REQUIRE(chain_literals[1].raw() == learned_clause[2].raw());
        REQUIRE(analyzer.has_learned_clause());

        conflict_analyzer variable_dedup_analyzer;
        const std::vector<literal> variable_dedup_conflict_clause {literal {variable {10}, false}, literal {variable {10}, true},
                                                                   literal {variable {11}, false}, literal {variable {12}, true}};
        variable_dedup_analyzer.seed_conflict_clause(std::span<const literal> {variable_dedup_conflict_clause});
        variable_dedup_analyzer.set_decision_level(variable {10}, 3u);
        variable_dedup_analyzer.set_decision_level(variable {11}, 2u);
        variable_dedup_analyzer.set_decision_level(variable {12}, 1u);
        variable_dedup_analyzer.analyze();

        const auto deduped_clause = variable_dedup_analyzer.learned_clause();
        REQUIRE(deduped_clause.size() == 3u);
        REQUIRE(deduped_clause[0].variable_of().index() == 10u);
        REQUIRE(deduped_clause[1].variable_of().index() == 11u);
        REQUIRE(deduped_clause[2].variable_of().index() == 12u);

        conflict_analyzer root_prune_analyzer;
        const std::vector<literal> root_prune_conflict_clause {literal {variable {20}, false}, literal {variable {21}, true},
                                                               literal {variable {22}, false}};
        root_prune_analyzer.seed_conflict_clause(std::span<const literal> {root_prune_conflict_clause});
        root_prune_analyzer.set_decision_level(variable {20}, 3u);
        root_prune_analyzer.set_decision_level(variable {21}, 0u);
        root_prune_analyzer.set_decision_level(variable {22}, 1u);
        root_prune_analyzer.analyze();

        const auto pruned_clause = root_prune_analyzer.learned_clause();
        REQUIRE(pruned_clause.size() == 2u);
        REQUIRE(pruned_clause[0].variable_of().index() == 20u);
        REQUIRE(pruned_clause[1].variable_of().index() == 22u);

        analyzer.analyze();
        REQUIRE_FALSE(analyzer.has_learned_clause());
        REQUIRE(analyzer.learned_clause().empty());

        analyzer.build_resolution_chain();
        REQUIRE(analyzer.resolution_chain_step_count() == 0u);
        REQUIRE(analyzer.resolution_chain_literals().empty());

        conflict_analyzer level_reset_analyzer;
        const std::vector<literal> first_conflict_clause {literal {variable {1}, false}, literal {variable {2}, true}};
        level_reset_analyzer.seed_conflict_clause(std::span<const literal> {first_conflict_clause});
        level_reset_analyzer.set_decision_level(variable {1}, 4);
        level_reset_analyzer.set_decision_level(variable {2}, 1);
        level_reset_analyzer.analyze();
        REQUIRE(level_reset_analyzer.compute_backjump_level() == 1u);

        const std::vector<literal> second_conflict_clause {literal {variable {1}, false}, literal {variable {2}, true}};
        level_reset_analyzer.seed_conflict_clause(std::span<const literal> {second_conflict_clause});
        level_reset_analyzer.set_decision_level(variable {1}, 1);
        level_reset_analyzer.analyze();
        REQUIRE(level_reset_analyzer.compute_backjump_level() == 0u);

        conflict_analyzer tied_level_analyzer;
        const std::vector<literal> tied_level_conflict_clause {literal {variable {31}, false}, literal {variable {32}, true},
                                                               literal {variable {33}, false}};
        tied_level_analyzer.seed_conflict_clause(std::span<const literal> {tied_level_conflict_clause});
        tied_level_analyzer.set_decision_level(variable {31}, 5u);
        tied_level_analyzer.set_decision_level(variable {32}, 5u);
        tied_level_analyzer.set_decision_level(variable {33}, 2u);
        tied_level_analyzer.analyze();
        REQUIRE(tied_level_analyzer.compute_backjump_level() == 2u);

        clause::learner learned_clause_registry;
        REQUIRE(!learned_clause_registry.learn_clause(std::span<const literal> {}).valid());

        const std::array<literal, 1> unit_clause {literal {variable {4}, false}};
        const auto unit_ref = learned_clause_registry.learn_clause(std::span<const literal> {unit_clause});
        REQUIRE(unit_ref.valid());
        REQUIRE(learned_clause_registry.learned_clause_count() == 1);

        const std::array<literal, 2> binary_clause {literal {variable {5}, false}, literal {variable {6}, true}};
        const auto binary_ref = learned_clause_registry.learn_clause(std::span<const literal> {binary_clause});
        REQUIRE(binary_ref.valid());
        REQUIRE(binary_ref.offset() > unit_ref.offset());
        REQUIRE(learned_clause_registry.learned_clause_count() == 2);

        const std::array<literal, 3> large_clause {literal {variable {7}, false}, literal {variable {8}, true},
                                                   literal {variable {9}, false}};
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

        // Real first-UIP resolution via analyze_via_resolution: two decisions (v1@1, v4@2), where v4's decision
        // propagates both v2 and v5 (reasons C and E) which together conflict via F. The walk must resolve through
        // both reasons, correctly deduplicating the shared ancestor v4, collapsing to the single asserting literal
        // ~v4 with a level-0 backjump (nothing survives below the current level).
        {
            struct resolution_fixture final
            {
                std::array<std::uint32_t, 6> levels {};
                std::array<std::vector<literal>, 6> reasons {};
            };

            resolution_fixture fixture {};
            fixture.levels[1] = 1u; // v1: decision, level 1 (unrelated ancestor, never touched by resolution)
            fixture.levels[4] = 2u; // v4: decision, level 2
            fixture.levels[2] = 2u; // v2: propagated via C, level 2
            fixture.levels[5] = 2u; // v5: propagated via E, level 2
            fixture.reasons[2] = {literal {variable {4}, true}, literal {variable {2}, false}}; // C = (~v4 v v2)
            fixture.reasons[5] = {literal {variable {4}, true}, literal {variable {5}, false}}; // E = (~v4 v v5)

            static const auto level_of = [](const void* context, const variable var) noexcept -> std::uint32_t
            {
                const auto* data = static_cast<const resolution_fixture*>(context);
                const auto index = static_cast<std::size_t>(var.index());
                return index < data->levels.size() ? data->levels[index] : 0u;
            };
            static const auto reason_of = [](const void* context, const variable var) noexcept -> std::span<const literal>
            {
                const auto* data = static_cast<const resolution_fixture*>(context);
                const auto index = static_cast<std::size_t>(var.index());
                return index < data->reasons.size() ? std::span<const literal> {data->reasons[index]} : std::span<const literal> {};
            };

            const std::vector<literal> trail_in_order {
                literal {variable {1}, false}, // v1 decided true, level 1
                literal {variable {4}, false}, // v4 decided true, level 2
                literal {variable {2}, false}, // v2 propagated true (reason C), level 2
                literal {variable {5}, false}, // v5 propagated true (reason E), level 2
            };

            conflict_analyzer multi_hop_analyzer;
            const std::vector<literal> multi_hop_conflict_clause {literal {variable {2}, true}, literal {variable {5}, true}}; // F
            multi_hop_analyzer.seed_conflict_clause(std::span<const literal> {multi_hop_conflict_clause});
            multi_hop_analyzer.analyze_via_resolution(std::span<const literal> {trail_in_order}, 2u, level_of, reason_of, &fixture);

            const auto multi_hop_clause = multi_hop_analyzer.learned_clause();
            REQUIRE(multi_hop_clause.size() == 1u);
            REQUIRE(multi_hop_clause[0].raw() == literal {variable {4}, true}.raw());
            REQUIRE(multi_hop_analyzer.derive_first_uip().raw() == literal {variable {4}, true}.raw());
            REQUIRE(multi_hop_analyzer.compute_backjump_level() == 0u);
            REQUIRE(multi_hop_analyzer.bump_candidate_count() == 3u);

            // Same fixture family, but the conflict clause now also carries a literal from an earlier non-zero
            // level (v3@1), which must survive into the learned clause's tail and drive a non-zero backjump.
            fixture.levels[3] = 1u;                                                             // v3: propagated via A, level 1
            fixture.reasons[3] = {literal {variable {1}, true}, literal {variable {3}, false}}; // A = (~v1 v v3)

            const std::vector<literal> tail_trail_in_order {
                literal {variable {1}, false}, // v1 decided true, level 1
                literal {variable {3}, false}, // v3 propagated true (reason A), level 1
                literal {variable {4}, false}, // v4 decided true, level 2
            };

            conflict_analyzer tail_retention_analyzer;
            const std::vector<literal> tail_conflict_clause {literal {variable {4}, true}, literal {variable {3}, true}};
            tail_retention_analyzer.seed_conflict_clause(std::span<const literal> {tail_conflict_clause});
            tail_retention_analyzer.analyze_via_resolution(std::span<const literal> {tail_trail_in_order}, 2u, level_of, reason_of,
                                                           &fixture);

            const auto tail_clause = tail_retention_analyzer.learned_clause();
            REQUIRE(tail_clause.size() == 2u);
            REQUIRE(tail_clause[0].raw() == literal {variable {4}, true}.raw());
            REQUIRE(tail_clause[1].raw() == literal {variable {3}, true}.raw());
            REQUIRE(tail_retention_analyzer.compute_backjump_level() == 1u);

            tail_retention_analyzer.build_resolution_chain();
            REQUIRE(tail_retention_analyzer.resolution_chain_step_count() == 1u);
        }

        // removed std::cout: "cdcl component state test passed\n";
    }

    TEST_CASE("first UIP preserves a nonzero multi-level backjump", "[sat]")
    {
        struct fixture final
        {
            std::array<std::uint32_t, 8> levels {};
            std::array<std::vector<literal>, 8> reasons {};
        } data;
        data.levels[1] = 1u;
        data.levels[2] = 2u;
        data.levels[3] = 3u;
        data.levels[4] = 2u;
        data.reasons[3] = {literal {variable {2u}, true}, literal {variable {3u}, false}};
        data.reasons[4] = {};

        const auto level_of = [](const void* context, const variable var) noexcept -> std::uint32_t
        {
            const auto* value = static_cast<const fixture*>(context);
            return value->levels[var.index()];
        };
        const auto reason_of = [](const void* context, const variable var) noexcept -> std::span<const literal>
        {
            const auto* value = static_cast<const fixture*>(context);
            return value->reasons[var.index()];
        };

        const std::array<literal, 4> trail {literal {variable {1u}, false}, literal {variable {2u}, false}, literal {variable {3u}, false},
                                            literal {variable {4u}, false}};
        conflict_analyzer analyzer;
        const std::array<literal, 2> conflict {literal {variable {3u}, true}, literal {variable {4u}, true}};
        analyzer.seed_conflict_clause(conflict);
        analyzer.analyze_via_resolution(trail, 3u, level_of, reason_of, &data);

        REQUIRE(analyzer.learned_clause().size() == 2u);
        REQUIRE(analyzer.derive_first_uip().raw() == literal {variable {3u}, true}.raw());
        REQUIRE(analyzer.compute_backjump_level() == 2u);
        REQUIRE(analyzer.learned_clause()[1].raw() == literal {variable {4u}, true}.raw());
        analyzer.build_resolution_chain();
        REQUIRE(analyzer.resolution_chain_step_count() == 1u);
    }

} // namespace
