#include <catch2/catch_test_macros.hpp>


#include <kmx/sat/literal.hpp>
#include <kmx/sat/simplify/engine/congruence.hpp>
#include <kmx/sat/simplify/engine/decomposition.hpp>
#include <kmx/sat/simplify/engine/instantiation.hpp>
#include <kmx/sat/simplify/engine/probing.hpp>
#include <kmx/sat/simplify/engine/sweep.hpp>
#include <kmx/sat/simplify/transitive_reducer.hpp>

namespace kmx::sat::simplify {

TEST_CASE("engine simplify", "[sat]")
{
    using namespace kmx::sat;
    using namespace kmx::sat::simplify;

    engine::decomposition decomposition;
    decomposition.run_scc();
    decomposition.find_equivalences();
    decomposition.emit_substitutions();
    REQUIRE(decomposition.component_count() == 1u);
    REQUIRE(decomposition.equivalence_count() == 0u);

    engine::probing probing;
    const literal lit {variable {4u}, false};
    probing.probe_literal(lit);
    probing.run_failed_literal_probing();
    probing.learn_hyper_binary();
    probing.record_backbone_candidate(lit);
    REQUIRE(probing.probe_count() == 1u);
    REQUIRE(probing.hyper_binary_count() == 1u);
    REQUIRE(probing.backbone_candidate_count() == 1u);

    transitive_reducer reducer;
    reducer.run();
    reducer.prune_binary_edges();
    reducer.report_removed_edges();
    REQUIRE(reducer.removed_edge_count() == 1u);

    engine::congruence congruence;
    congruence.apply_gate_constraints();
    congruence.derive_equivalences();
    congruence.run();
    REQUIRE(congruence.gate_constraint_count() == 1u);
    REQUIRE(congruence.equivalence_count() == 1u);
    REQUIRE(congruence.run_completed());

    engine::instantiation instantiation;
    instantiation.collect_candidates();
    instantiation.instantiate_literal_removal();
    instantiation.run();
    REQUIRE(instantiation.candidate_count() == 1u);
    REQUIRE(instantiation.removal_count() == 1u);
    REQUIRE(instantiation.run_completed());

    engine::sweep sweep;
    sweep.build_micro_instance();
    sweep.extract_backbone();
    sweep.extract_equivalences();
    sweep.transfer_facts();
    sweep.run();
    REQUIRE(sweep.micro_instance_built());
    REQUIRE(sweep.backbone_count() == 1u);
    REQUIRE(sweep.equivalence_count() == 1u);
    REQUIRE(sweep.transferred());

    // removed std::cout: "engine simplify test passed\n";
    }

} // namespace
