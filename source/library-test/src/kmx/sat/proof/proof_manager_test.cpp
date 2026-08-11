#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

#include <kmx/sat/proof/clause/id_allocator.hpp>
#include <kmx/sat/proof/event_stream.hpp>
#include <kmx/sat/proof/tracer/view.hpp>
#include <kmx/sat/proof/tracer/variant_t.hpp>
#include <kmx/sat/proof/tracer/like.hpp>
#include <kmx/sat/proof/clause/id.hpp>
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/io/proof_output_pipeline.hpp>
#include <kmx/sat/io/writer/kmx_aio_proof.hpp>
#include <kmx/sat/proof_manager.hpp>
#include <kmx/sat/runtime/co_fsm_adapter.hpp>
#include <kmx/sat/runtime/controller/portfolio.hpp>
#include <kmx/sat/runtime/controller/signal.hpp>
#include <kmx/sat/runtime/parallel_preprocess_executor.hpp>
#include <kmx/sat/runtime/shared_clause_exchange.hpp>

namespace kmx::sat::proof {

TEST_CASE("proof manager", "[sat]")
{
    using namespace kmx::sat;
    using namespace kmx::sat::proof;
    const cdcl::clause::ref_t ref_1 {1};
    const cdcl::clause::ref_t ref_2 {2};
    const cdcl::clause::ref_t ref_7 {7};
    const cdcl::clause::ref_t ref_8 {8};
    const cdcl::clause::ref_t ref_42 {42};
    const cdcl::clause::ref_t ref_70 {70};

    clause::id_allocator allocator;
    const auto first = allocator.allocate_for_new_clause(ref_1);
    const auto second = allocator.allocate_for_new_clause(ref_2);

    REQUIRE(first.valid());
    REQUIRE(second.valid());
    REQUIRE(first.value() != second.value());
    REQUIRE(allocator.id_for_clause_ref(ref_1).equals(first));

    allocator.preserve_on_relocation(ref_1, ref_42);
    REQUIRE(!allocator.id_for_clause_ref(ref_1).valid());
    REQUIRE(allocator.id_for_clause_ref(ref_42).equals(first));

    allocator.retire_on_delete(second);
    REQUIRE(!allocator.has_active_id(second));
    allocator.retire_on_delete(ref_42);
    REQUIRE(!allocator.has_active_id(first));

    proof_manager manager;
    manager.enable_format("drat");
    manager.enable_format("lrat");
    manager.enable_checker("online");
    manager.enable_checker("lrat");

    const variable var_x {1};
    const variable var_y {2};
    const variable var_z {3};
    const literal x_pos {var_x, false};
    const literal x_neg {var_x, true};
    const literal y_pos {var_y, false};
    const literal z_pos {var_z, false};

    // Standalone clause exercising the relocate/shrink/delete lifecycle, independent of the derivation chain below.
    const cdcl::clause::ref_t ref_11 {11};
    const std::array<literal, 2> lifecycle_literals {x_pos, y_pos};
    manager.on_add_original(ref_11, lifecycle_literals);
    const auto lifecycle_id = manager.stable_id_for_clause(ref_11);
    REQUIRE(lifecycle_id.valid());

    manager.on_clause_relocated(ref_11, ref_70);
    REQUIRE(!manager.stable_id_for_clause(ref_11).valid());
    REQUIRE(manager.stable_id_for_clause(ref_70).equals(lifecycle_id));

    const std::array<literal, 1> shrunk_literals {x_pos};
    manager.on_shrink_clause(ref_70, shrunk_literals);
    REQUIRE(manager.last_event().kind == event_kind::shrink_clause);
    REQUIRE(manager.last_event().clause_id.equals(lifecycle_id));
    REQUIRE(manager.last_event().literals.size() == 1);
    REQUIRE(manager.last_event().literals[0] == 1);

    manager.on_delete_clause(ref_70);
    REQUIRE(manager.last_event().kind == event_kind::delete_clause);
    REQUIRE(manager.last_event().clause_id.equals(lifecycle_id));
    REQUIRE(!manager.stable_id_for_clause(ref_70).valid());

    // Original clause (x v y), kept alive as an antecedent for the derivation below.
    const std::array<literal, 2> original_literals {x_pos, y_pos};
    manager.on_add_original(ref_7, original_literals);
    const auto original_id = manager.stable_id_for_clause(ref_7);
    REQUIRE(original_id.valid());

    // Second original clause (-x v z), used with the first to derive unit (y v z) by resolving on x.
    const cdcl::clause::ref_t ref_9 {9};
    const std::array<literal, 2> second_original_literals {x_neg, z_pos};
    manager.on_add_original(ref_9, second_original_literals);
    const auto second_original_id = manager.stable_id_for_clause(ref_9);
    REQUIRE(second_original_id.valid());

    // Derived clause justified by a real, verifiable antecedent chain recorded in the same call.
    const std::array<literal, 2> derived_literals {y_pos, z_pos};
    const std::array<clause::id, 2> antecedents {original_id, second_original_id};
    manager.on_add_derived(ref_8, derived_literals, antecedents);
    const auto derived_id = manager.stable_id_for_clause(ref_8);
    REQUIRE(derived_id.valid());
    REQUIRE(!derived_id.equals(original_id));
    REQUIRE(manager.last_event().literals.size() == 2);
    REQUIRE(manager.last_event().antecedent_ids.size() == 2);
    REQUIRE(manager.last_event().antecedent_ids[0].equals(original_id));
    REQUIRE(manager.last_event().antecedent_ids[1].equals(second_original_id));

    REQUIRE(manager.checker_coverage().clauses_added == 4);
    REQUIRE(manager.checker_coverage().clauses_shrunk == 1);
    REQUIRE(manager.checker_coverage().clauses_deleted == 1);
    REQUIRE(manager.validate_checkers());

    // Feeding a chain that does not actually resolve to the derived clause must fail validation.
    const std::array<clause::id, 1> bad_chain {original_id};
    manager.record_checker_antecedents(derived_id, bad_chain);
    REQUIRE(!manager.validate_checkers());

    REQUIRE(manager.buffered_event_count() >= 4);
    manager.on_conclusion();
    REQUIRE(manager.last_event().kind == event_kind::conclusion);
    REQUIRE(manager.last_event().finalized);
    manager.flush();
    REQUIRE(manager.buffered_event_count() == 0);

    constexpr bool tracer_concept_ok = tracer::like<tracer::drat>;
    static_assert(tracer_concept_ok, "drat tracer must satisfy tracer::like");

    SECTION("event stream sink receives buffered proof events")
    {
        proof_manager sink_manager;
        std::vector<proof::proof_event> sink_events;
        sink_manager.set_event_sink([&](const proof::proof_event& event) noexcept {
            sink_events.push_back(event);
        });

        const std::array<literal, 1> single_literal {x_pos};
        sink_manager.on_add_original(ref_1, single_literal);
        sink_manager.on_conclusion();
        sink_manager.flush();

        REQUIRE(sink_events.size() == 2u);
        REQUIRE(sink_events[0].kind == event_kind::add_original);
        REQUIRE(sink_events[1].kind == event_kind::conclusion);
    }

    SECTION("proof output pipeline submits buffered events")
    {
        kmx::sat::io::proof_output_pipeline pipeline;
        pipeline.start();

        proof::event_stream stream;
        stream.push_event({event_kind::add_original, ref_1});
        stream.push_event({event_kind::conclusion, ref_1, {}, {}, {}, true});
        pipeline.submit(stream);

        REQUIRE(pipeline.submitted_count() == 3u);
    }

    SECTION("kmx aio proof writer tracks open and flush state")
    {
        kmx::sat::io::writer::kmx_aio_proof writer;
        std::array<std::byte, 2> bytes {std::byte {1}, std::byte {2}};

        writer.open_sink();
        writer.submit_buffer(bytes);
        writer.await_flush();
        writer.close_sink();

        REQUIRE(writer.opened());
        REQUIRE(writer.submitted_count() == 1u);
        REQUIRE(writer.flushed());
        REQUIRE(writer.closed());
    }

    SECTION("co fsm adapter is a documented placeholder")
    {
        kmx::sat::runtime::co_fsm_adapter adapter;
        REQUIRE(adapter.active() == false);
        adapter.activate();
        REQUIRE(adapter.active());
        adapter.deactivate();
        REQUIRE(!adapter.active());
    }

    SECTION("signal controller tracks stop requests")
    {
        kmx::sat::runtime::controller::signal controller;
        REQUIRE(!controller.termination_requested());

        controller.install_handlers();
        controller.request_stop();
        REQUIRE(controller.termination_requested());

        controller.clear();
        REQUIRE(!controller.termination_requested());
    }

    SECTION("parallel preprocess executor tracks deterministic pass execution")
    {
        kmx::sat::runtime::parallel_preprocess_executor executor;
        REQUIRE(executor.thread_budget() == 1u);

        executor.run_parallel_pass();
        executor.merge_deterministic_result();

        REQUIRE(executor.thread_budget() == 1u);
        REQUIRE(executor.runs() == 1u);
        REQUIRE(executor.merges() == 1u);
    }

    SECTION("portfolio controller tracks launch and cancellation state")
    {
        kmx::sat::runtime::controller::portfolio controller;
        REQUIRE(!controller.launched());
        REQUIRE(!controller.cancelled());
        REQUIRE(!controller.collected());

        controller.launch_strategies();
        REQUIRE(controller.launched());

        controller.cancel_others_on_result();
        REQUIRE(controller.cancelled());

        controller.collect_winner();
        REQUIRE(controller.collected());
    }

    SECTION("shared clause exchange tracks publish and drain state")
    {
        kmx::sat::runtime::shared_clause_exchange exchange;
        const cdcl::clause::ref_t ref {9};

        REQUIRE(!exchange.has_pending());
        exchange.publish_clause(ref);
        REQUIRE(exchange.has_pending());
        exchange.apply_exchange_policy();
        exchange.drain_incoming();
        REQUIRE(exchange.drain_count() == 1u);
        exchange.reset_exchange_state();
        REQUIRE(!exchange.has_pending());
    }

    SECTION("lrat tracer records the emitted proof events")
    {
        tracer::lrat lrat_tracer;
        const cdcl::clause::ref_t ref_7 {7u};

        lrat_tracer.add_original(ref_7);
        lrat_tracer.add_derived(ref_7);
        lrat_tracer.delete_clause(ref_7);
        lrat_tracer.shrink_clause(ref_7);
        lrat_tracer.finalize();

        REQUIRE(lrat_tracer.finalized());
        REQUIRE(lrat_tracer.emitted_count() == 5u);

        const auto& recorded = lrat_tracer.emitted_events();
        REQUIRE(recorded.size() == 5u);
        REQUIRE(recorded[0].kind == tracer::lrat::event_kind::add_original);
        REQUIRE(recorded[0].ref_offset == ref_7.offset());
        REQUIRE(recorded[1].kind == tracer::lrat::event_kind::add_derived);
        REQUIRE(recorded[2].kind == tracer::lrat::event_kind::delete_clause);
        REQUIRE(recorded[3].kind == tracer::lrat::event_kind::shrink_clause);
        REQUIRE(recorded[4].kind == tracer::lrat::event_kind::finalize);
    }

    // removed std::cout: "proof manager test passed\n";
    }

} // namespace
