#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <kmx/sat/cdcl/compaction_service.hpp>
#include <kmx/sat/cdcl/external_frontend.hpp>
#include <kmx/sat/cdcl/failed_core_extractor.hpp>
#include <kmx/sat/cdcl/model_reconstructor.hpp>
#include <kmx/sat/cdcl/stack/extension.hpp>
#include <kmx/sat/cdcl/variable_mapper.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl {

TEST_CASE("model reconstructor drops factor-introduced variables when replaying extension records", "[sat]")
{
    using namespace kmx::sat;
    using namespace kmx::sat::cdcl;

    model_reconstructor reconstructor;
    const std::vector<literal> initial_model {
        literal {variable {4u}, false},
        literal {variable {6u}, true},
    };
    reconstructor.set_initial_model(initial_model);

    extension_record record;
    record.payload = extension_record::factor_transformation {variable {6u}};
    reconstructor.apply_extension_record(record);

    const model_view view = reconstructor.reconstruct_full_model();
    REQUIRE(view.values().size() == 1u);
    REQUIRE(view.values()[0].variable_of().index() == 4u);
}

TEST_CASE("external frontend integration", "[sat]")
{
    using namespace kmx::sat;
    using namespace kmx::sat::cdcl;

    variable_mapper mapper;
    external_frontend frontend {mapper};
    const literal external_lit {variable {4u}, false};
    const literal internal_lit = frontend.import_external_literal(external_lit);
    REQUIRE(internal_lit.variable_of().index() > 0u);
    REQUIRE(frontend.export_internal_literal(internal_lit).variable_of().index() == external_lit.variable_of().index());

    frontend.freeze_variable(variable {4u});
    REQUIRE(mapper.is_inactive(variable {4u}));
    frontend.melt_variable(variable {4u});
    REQUIRE(!mapper.is_inactive(variable {4u}));
    frontend.push_assumption(external_lit);
    REQUIRE(frontend.has_pending_assumptions());
    frontend.prepare_solve_request();
    REQUIRE(frontend.assumptions().size() == 1u);
    REQUIRE(frontend.prepared_request().assumptions.size() == 1u);

    solve_request prepared_request {};
    prepared_request.conflict_limit = 3u;
    prepared_request.decision_limit = 7u;
    prepared_request.enabled_pass_mask = 13u;
    prepared_request.strict_mode = true;
    frontend.apply_prepared_request(prepared_request);
    REQUIRE(frontend.prepared_request().conflict_limit == 3u);
    REQUIRE(frontend.prepared_request().decision_limit == 7u);
    REQUIRE(frontend.prepared_request().enabled_pass_mask == 13u);
    REQUIRE(frontend.prepared_request().strict_mode);

    model_reconstructor reconstructor;
    const std::vector<literal> initial_model {
        literal {variable {4u}, false},
        literal {variable {6u}, true},
    };
    reconstructor.set_initial_model(initial_model);
    reconstructor.mark_internal_only_variable(variable {6u});
    extension_record record;
    record.payload = extension_record::factor_transformation {variable {6u}};
    reconstructor.apply_extension_record(record);
    reconstructor.drop_internal_only_variables();
    const model_view view = reconstructor.reconstruct_full_model();
    REQUIRE(view.values().size() == 1u);
    REQUIRE(view.values()[0].variable_of().index() == 4u);

    failed_core_extractor core_extractor;
    core_extractor.mark_failed_assumption(literal {variable {2u}, true});
    core_extractor.mark_failed_assumption(literal {variable {3u}, false});
    const failed_core_view failed_core = core_extractor.build_failed_core();
    REQUIRE(failed_core.assumptions().size() == 2u);

    const variable external_variable {9u};
    const variable internal_variable = mapper.ensure_external_variable(external_variable);
    compaction_service service;
    service.attach_mapper(mapper);
    service.build_variable_permutation();
    service.rewrite_external_mapping();
    REQUIRE(service.should_compact() == true);
    REQUIRE(mapper.to_internal_literal(literal {external_variable, false}).variable_of().index() == internal_variable.index());

    SECTION("extension stack records reversible transformations")
    {
        stack::extension extension_stack;
        extension_record factor_record;
        factor_record.payload = extension_record::factor_transformation {variable {6u}};

        extension_stack.push_factor_record(factor_record);
        REQUIRE(extension_stack.size() == 1u);
        REQUIRE(extension_stack.records().size() == 1u);
        REQUIRE(std::holds_alternative<extension_record::factor_transformation>(extension_stack.records()[0].payload));

        extension_stack.clear_all();
        REQUIRE(extension_stack.size() == 0u);
    }

    // removed std::cout: "external frontend integration test passed\n";
    }

} // namespace
