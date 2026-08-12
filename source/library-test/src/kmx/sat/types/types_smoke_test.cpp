#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>

#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/failed_core_view.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/model_view.hpp>
#include <kmx/sat/proof/clause/id.hpp>
#include <kmx/sat/solve_request.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat
{
    TEST_CASE("types api", "[sat]")
    {
        const variable var {7u};
        const literal pos {var, false};
        const literal neg = pos.negated();

        REQUIRE(var.index() == 7u);
        REQUIRE(pos.variable_of().index() == 7u);
        REQUIRE_FALSE(pos.is_negated());
        REQUIRE(neg.is_negated());
        REQUIRE(neg.variable_of().index() == 7u);
        REQUIRE(pos.index_in_watch_bank() != neg.index_in_watch_bank());

        const std::array<literal, 2> model_literals {pos, literal {variable {8u}, true}};
        const model_view model {std::span<const literal> {model_literals}};
        REQUIRE(model.values().size() == 2u);
        REQUIRE(model.values()[0].raw() == pos.raw());

        const std::array<literal, 1> failed_assumptions {neg};
        const failed_core_view failed_core {std::span<const literal> {failed_assumptions}};
        REQUIRE(failed_core.assumptions().size() == 1u);
        REQUIRE(failed_core.assumptions()[0].raw() == neg.raw());

        solve_request request {};
        request.assumptions.assign(failed_core.assumptions().begin(), failed_core.assumptions().end());
        request.conflict_limit = 12u;
        request.decision_limit = 34u;
        request.strict_mode = true;
        REQUIRE(request.assumptions.size() == 1u);
        REQUIRE(request.conflict_limit == 12u);
        REQUIRE(request.decision_limit == 34u);
        REQUIRE(request.strict_mode);

        const cdcl::clause::ref_t invalid_ref {};
        const cdcl::clause::ref_t live_ref {42u};
        REQUIRE(invalid_ref.invalid());
        REQUIRE(live_ref.valid());
        REQUIRE(live_ref.offset() == 42u);

        const proof::clause::id invalid_id {};
        const proof::clause::id live_id {9u};
        REQUIRE_FALSE(invalid_id.valid());
        REQUIRE(live_id.valid());
        REQUIRE(live_id.value() == 9u);
    }
}