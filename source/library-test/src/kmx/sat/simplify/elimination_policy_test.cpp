#include <catch2/catch_test_macros.hpp>


#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/simplify/eliminator/clause/blocked.hpp>
#include <kmx/sat/simplify/eliminator/clause/covered.hpp>
#include <kmx/sat/simplify/flush_restore_manager.hpp>
#include <kmx/sat/simplify/preprocessing_profile_selector.hpp>
#include <kmx/sat/simplify/vivifier.hpp>

namespace kmx::sat::simplify {

TEST_CASE("elimination policy", "[sat]")
{
    using namespace kmx::sat;
    using namespace kmx::sat::simplify;

    eliminator::clause::blocked blocked;
    blocked.run();
    REQUIRE(blocked.blocked_count() == 1u);
    const auto blocked_literal = blocked.last_blocked_literal();
    const auto expected_literal = literal {variable {2u}, true};
    REQUIRE(blocked_literal.raw() == expected_literal.raw());

    eliminator::clause::covered covered;
    covered.compute_covered_literals(cdcl::clause::ref_t {0u});
    covered.mark_covered(cdcl::clause::ref_t {0u});
    covered.run();
    REQUIRE(covered.covered_count() == 1u);

    flush_restore_manager flush_restore;
    flush_restore.flush_redundant();
    flush_restore.restore_irredundant_only();
    flush_restore.remove_satisfied();
    REQUIRE(flush_restore.flush_count() == 1u);
    REQUIRE(flush_restore.restore_count() == 1u);
    REQUIRE(flush_restore.satisfied_removed_count() == 1u);

    preprocessing_profile_selector selector;
    selector.fingerprint_formula();
    selector.select_pass_plan();
    selector.record_pass_effectiveness();
    selector.update_selection_policy();
    REQUIRE(selector.fingerprint_count() == 1u);
    REQUIRE(selector.pass_plan_count() == 1u);
    REQUIRE(selector.effectiveness_count() == 1u);
    REQUIRE(selector.policy_update_count() == 1u);

    vivifier viv;
    viv.vivify_clause(cdcl::clause::ref_t {1u});
    viv.commit_shrunk_clause(cdcl::clause::ref_t {1u});
    viv.run();
    REQUIRE(viv.vivified_clause_count() == 1u);
    REQUIRE(viv.committed_shrink_count() == 1u);
    REQUIRE(viv.run_completed());

    // removed std::cout: "elimination policy test passed\n";
    }

} // namespace
