#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>
#include <span>

#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/failed_core_view.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/model_view.hpp>
#include <kmx/sat/proof/clause/id.hpp>
#include <kmx/sat/solve_request.hpp>
#include <kmx/sat/solve_result.hpp>
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

        const std::array<literal, 2u> model_literals {pos, literal {variable {8u}, true}};
        const model_view model {std::span<const literal> {model_literals}};
        REQUIRE(model.values().size() == 2u);
        REQUIRE(model.values()[0u].raw() == pos.raw());

        const std::array<literal, 1u> failed_assumptions {neg};
        const failed_core_view failed_core {std::span<const literal> {failed_assumptions}};
        REQUIRE(failed_core.assumptions().size() == 1u);
        REQUIRE(failed_core.assumptions()[0u].raw() == neg.raw());

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

        const variable default_variable {};
        REQUIRE(default_variable.index() == 0u);
        REQUIRE(default_variable < var);
        REQUIRE(var == variable {7u});

        constexpr auto maximum_variable_index = std::numeric_limits<variable::index_t>::max() >> 1u;
        const variable maximum_variable {maximum_variable_index};
        const literal maximum_positive {maximum_variable, false};
        const literal maximum_negative {maximum_variable, true};
        REQUIRE(maximum_positive.variable_of().index() == maximum_variable_index);
        REQUIRE(maximum_negative.variable_of().index() == maximum_variable_index);
        REQUIRE(maximum_positive.negated() == maximum_negative);
        REQUIRE(maximum_positive.raw() + 1u == maximum_negative.raw());

        const literal zero_literal {};
        REQUIRE(zero_literal.raw() == 0u);
        REQUIRE(zero_literal.variable_of().index() == 0u);
        REQUIRE_FALSE(zero_literal.is_negated());
        REQUIRE(zero_literal.negated().raw() == 1u);

        const model_view empty_model {};
        const failed_core_view empty_failed_core {};
        REQUIRE(empty_model.values().empty());
        REQUIRE(empty_failed_core.assumptions().empty());

        const solve_result default_result {};
        REQUIRE(default_result.status_of() == solve_result::status::unknown);
        REQUIRE(default_result.model().values().empty());
        REQUIRE(default_result.failed_core().assumptions().empty());
        REQUIRE_FALSE(default_result.proof_summary_of().proof_enabled);
        REQUIRE_FALSE(default_result.proof_summary_of().proof_checked);

        const auto statistics = default_result.statistics_snapshot();
        REQUIRE(statistics.conflicts == 0u);
        REQUIRE(statistics.decisions == 0u);
        REQUIRE(statistics.propagations == 0u);
    }
}