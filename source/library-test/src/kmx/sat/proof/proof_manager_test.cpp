#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof/clause/id.hpp>
#include <kmx/sat/proof/clause/id_allocator.hpp>
#include <kmx/sat/proof/event_stream.hpp>
#include <kmx/sat/proof/tracer/like.hpp>
#include <kmx/sat/proof/tracer/variant_t.hpp>
#include <kmx/sat/proof/tracer/view.hpp>
#include <kmx/sat/proof_manager.hpp>

namespace kmx::sat::proof
{

    TEST_CASE("proof manager", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::proof;
        const cdcl::clause::ref_t ref_1 {1u};
        const cdcl::clause::ref_t ref_2 {2u};
        const cdcl::clause::ref_t ref_7 {7u};
        const cdcl::clause::ref_t ref_8 {8u};
        const cdcl::clause::ref_t ref_42 {42u};
        const cdcl::clause::ref_t ref_70 {70u};

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
        manager.enable_format(format_id::drat);
        manager.enable_format(format_id::lrat);
        manager.enable_checker(checker_id::online);
        manager.enable_checker(checker_id::lrat);

        const variable var_x {1u};
        const variable var_y {2u};
        const variable var_z {3u};
        const literal x_pos {var_x, false};
        const literal x_neg {var_x, true};
        const literal y_pos {var_y, false};
        const literal z_pos {var_z, false};

        // Standalone clause exercising the relocate/shrink/delete lifecycle, independent of the derivation chain below.
        const cdcl::clause::ref_t ref_11 {11u};
        const std::array<literal, 2u> lifecycle_literals {x_pos, y_pos};
        manager.on_add_original(ref_11, lifecycle_literals);
        const auto lifecycle_id = manager.stable_id_for_clause(ref_11);
        REQUIRE(lifecycle_id.valid());

        manager.on_clause_relocated(ref_11, ref_70);
        REQUIRE(!manager.stable_id_for_clause(ref_11).valid());
        REQUIRE(manager.stable_id_for_clause(ref_70).equals(lifecycle_id));

        const std::array<literal, 1u> shrunk_literals {x_pos};
        manager.on_shrink_clause(ref_70, shrunk_literals);
        REQUIRE(manager.last_event().kind == event_kind::shrink_clause);
        REQUIRE(manager.last_event().clause_id.equals(lifecycle_id));
        REQUIRE(manager.last_event().literals.size() == 1u);
        REQUIRE(manager.last_event().literals[0u] == 1);

        manager.on_delete_clause(ref_70);
        REQUIRE(manager.last_event().kind == event_kind::delete_clause);
        REQUIRE(manager.last_event().clause_id.equals(lifecycle_id));
        REQUIRE(!manager.stable_id_for_clause(ref_70).valid());

        // Original clause (x v y), kept alive as an antecedent for the derivation below.
        const std::array<literal, 2u> original_literals {x_pos, y_pos};
        manager.on_add_original(ref_7, original_literals);
        const auto original_id = manager.stable_id_for_clause(ref_7);
        REQUIRE(original_id.valid());

        // Second original clause (-x v z), used with the first to derive unit (y v z) by resolving on x.
        const cdcl::clause::ref_t ref_9 {9u};
        const std::array<literal, 2u> second_original_literals {x_neg, z_pos};
        manager.on_add_original(ref_9, second_original_literals);
        const auto second_original_id = manager.stable_id_for_clause(ref_9);
        REQUIRE(second_original_id.valid());

        // Derived clause justified by a real, verifiable antecedent chain recorded in the same call.
        const std::array<literal, 2u> derived_literals {y_pos, z_pos};
        const std::array<clause::id, 2u> antecedents {original_id, second_original_id};
        manager.on_add_derived(ref_8, derived_literals, antecedents);
        const auto derived_id = manager.stable_id_for_clause(ref_8);
        REQUIRE(derived_id.valid());
        REQUIRE(!derived_id.equals(original_id));
        REQUIRE(manager.last_event().literals.size() == 2u);
        REQUIRE(manager.last_event().antecedent_ids.size() == 2u);
        REQUIRE(manager.last_event().antecedent_ids[0u].equals(original_id));
        REQUIRE(manager.last_event().antecedent_ids[1u].equals(second_original_id));

        REQUIRE(manager.checker_coverage().clauses_added == 4u);
        REQUIRE(manager.checker_coverage().clauses_shrunk == 1u);
        REQUIRE(manager.checker_coverage().clauses_deleted == 1u);
        REQUIRE(manager.validate_checkers());

        // Feeding a chain that does not actually resolve to the derived clause must fail validation.
        const std::array<clause::id, 1u> bad_chain {original_id};
        manager.record_checker_antecedents(derived_id, bad_chain);
        REQUIRE(!manager.validate_checkers());

        constexpr bool tracer_concept_ok = tracer::like<tracer::drat>;
        static_assert(tracer_concept_ok, "drat tracer must satisfy tracer::like");

        REQUIRE(manager.buffered_event_count() >= 4u);
        manager.on_conclusion();
        REQUIRE(manager.last_event().kind == event_kind::conclusion);
        REQUIRE(manager.last_event().finalized);
        manager.flush();
        REQUIRE(manager.buffered_event_count() == 0u);

        SECTION("event stream sink receives buffered proof events")
        {
            proof_manager sink_manager;
            std::vector<proof::proof_event> sink_events;
            sink_manager.set_event_sink([&](const proof::proof_event& event) noexcept { sink_events.push_back(event); });

            const std::array<literal, 1u> single_literal {x_pos};
            sink_manager.on_add_original(ref_1, single_literal);
            sink_manager.on_conclusion();
            sink_manager.flush();

            REQUIRE(sink_events.size() == 2u);
            REQUIRE(sink_events[0u].kind == event_kind::add_original);
            REQUIRE(sink_events[1u].kind == event_kind::conclusion);
            REQUIRE(sink_events[1u].finalized);
        }

        SECTION("buffered event view keeps antecedent payload order")
        {
            proof_manager view_manager;
            view_manager.enable_checker(checker_id::lrat);

            const cdcl::clause::ref_t ref_a {101u};
            const cdcl::clause::ref_t ref_b {102u};
            const cdcl::clause::ref_t ref_c {103u};

            const std::array<literal, 2u> clause_a {x_pos, y_pos};
            const std::array<literal, 2u> clause_b {x_neg, z_pos};
            const std::array<literal, 2u> clause_c {y_pos, z_pos};

            view_manager.on_add_original(ref_a, clause_a);
            view_manager.on_add_original(ref_b, clause_b);

            const auto id_a = view_manager.stable_id_for_clause(ref_a);
            const auto id_b = view_manager.stable_id_for_clause(ref_b);
            REQUIRE(id_a.valid());
            REQUIRE(id_b.valid());

            const std::array<clause::id, 2u> ordered_antecedents {id_b, id_a};
            view_manager.on_add_derived(ref_c, clause_c, ordered_antecedents);

            const auto events = view_manager.buffered_events();
            REQUIRE(events.size() == view_manager.buffered_event_count());
            REQUIRE(events.size() == 3u);
            REQUIRE(events[2u].kind == event_kind::add_derived);
            REQUIRE(events[2u].antecedent_ids.size() == 2u);
            REQUIRE(events[2u].antecedent_ids[0u].equals(id_b));
            REQUIRE(events[2u].antecedent_ids[1u].equals(id_a));

            view_manager.flush();
            REQUIRE(view_manager.buffered_event_count() == 0u);
            REQUIRE(view_manager.buffered_events().empty());
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
            REQUIRE(recorded[0u].kind == tracer::lrat::event_kind::add_original);
            REQUIRE(recorded[0u].ref_offset == ref_7.offset());
            REQUIRE(recorded[1u].kind == tracer::lrat::event_kind::add_derived);
            REQUIRE(recorded[2u].kind == tracer::lrat::event_kind::delete_clause);
            REQUIRE(recorded[3u].kind == tracer::lrat::event_kind::shrink_clause);
            REQUIRE(recorded[4u].kind == tracer::lrat::event_kind::finalize);
        }

        SECTION("lrat tracer keeps literals and antecedent ids from payload events")
        {
            tracer::lrat lrat_tracer;

            proof::proof_event payload {};
            payload.kind = proof::event_kind::add_derived;
            payload.clause_ref = ref_8;
            payload.clause_id = clause::id {17u};
            payload.literals = {2, 3};
            payload.antecedent_ids = {clause::id {4u}, clause::id {9u}};

            lrat_tracer.on_event(payload);

            REQUIRE(lrat_tracer.emitted_count() == 1u);
            const auto& recorded = lrat_tracer.emitted_events();
            REQUIRE(recorded[0u].kind == tracer::lrat::event_kind::add_derived);
            REQUIRE(recorded[0u].ref_offset == ref_8.offset());
            REQUIRE(recorded[0u].clause_id_value == 17u);
            REQUIRE(recorded[0u].literals.size() == 2u);
            REQUIRE(recorded[0u].literals[0u] == 2);
            REQUIRE(recorded[0u].literals[1u] == 3);
            REQUIRE(recorded[0u].antecedent_id_values.size() == 2u);
            REQUIRE(recorded[0u].antecedent_id_values[0u] == 4u);
            REQUIRE(recorded[0u].antecedent_id_values[1u] == 9u);
        }

        SECTION("idrup tracer records epoch-scoped proof events")
        {
            tracer::idrup idrup_tracer;
            const cdcl::clause::ref_t ref_5 {5u};

            idrup_tracer.add_original(ref_5);
            idrup_tracer.add_derived(ref_5);
            idrup_tracer.delete_clause(ref_5);
            idrup_tracer.shrink_clause(ref_5);
            idrup_tracer.finalize();

            REQUIRE(idrup_tracer.finalized());
            REQUIRE(idrup_tracer.current_epoch() == 1u);
            REQUIRE(idrup_tracer.emitted_count() == 5u);

            const auto& recorded = idrup_tracer.emitted_events();
            REQUIRE(recorded[0u].kind == tracer::idrup::event_kind::add_original);
            REQUIRE(recorded[0u].epoch == 0u);
            REQUIRE(recorded[1u].kind == tracer::idrup::event_kind::add_derived);
            REQUIRE(recorded[2u].kind == tracer::idrup::event_kind::delete_clause);
            REQUIRE(recorded[3u].kind == tracer::idrup::event_kind::shrink_clause);
            REQUIRE(recorded[4u].kind == tracer::idrup::event_kind::finalize);
        }

        SECTION("drat tracer records the emitted proof events")
        {
            tracer::drat drat_tracer;
            const cdcl::clause::ref_t ref_6 {6u};

            drat_tracer.add_original(ref_6);
            drat_tracer.add_derived(ref_6);
            drat_tracer.delete_clause(ref_6);
            drat_tracer.shrink_clause(ref_6);
            drat_tracer.finalize();

            REQUIRE(drat_tracer.finalized());
            REQUIRE(drat_tracer.emitted_count() == 5u);

            const auto& recorded = drat_tracer.emitted_events();
            REQUIRE(recorded[0u].kind == tracer::drat::event_kind::add_original);
            REQUIRE(recorded[1u].kind == tracer::drat::event_kind::add_derived);
            REQUIRE(recorded[2u].kind == tracer::drat::event_kind::delete_clause);
            REQUIRE(recorded[3u].kind == tracer::drat::event_kind::shrink_clause);
            REQUIRE(recorded[4u].kind == tracer::drat::event_kind::finalize);
        }

        SECTION("frat tracer records the emitted proof events")
        {
            tracer::frat frat_tracer;
            const cdcl::clause::ref_t ref_10 {10u};

            frat_tracer.add_original(ref_10);
            frat_tracer.add_derived(ref_10);
            frat_tracer.delete_clause(ref_10);
            frat_tracer.shrink_clause(ref_10);
            frat_tracer.finalize();

            REQUIRE(frat_tracer.finalized());
            REQUIRE(frat_tracer.emitted_count() == 5u);

            const auto& recorded = frat_tracer.emitted_events();
            REQUIRE(recorded[0u].kind == tracer::frat::event_kind::add_original);
            REQUIRE(recorded[1u].kind == tracer::frat::event_kind::add_derived);
            REQUIRE(recorded[2u].kind == tracer::frat::event_kind::delete_clause);
            REQUIRE(recorded[3u].kind == tracer::frat::event_kind::shrink_clause);
            REQUIRE(recorded[4u].kind == tracer::frat::event_kind::finalize);
        }

        SECTION("frat tracer keeps literals and antecedent ids from payload events")
        {
            tracer::frat frat_tracer;
            const cdcl::clause::ref_t ref_10 {10u};

            proof::proof_event payload {};
            payload.kind = proof::event_kind::add_derived;
            payload.clause_ref = ref_10;
            payload.clause_id = clause::id {21u};
            payload.literals = {1, -2, 3};
            payload.antecedent_ids = {clause::id {5u}, clause::id {7u}};

            frat_tracer.on_event(payload);

            REQUIRE(frat_tracer.emitted_count() == 1u);
            const auto& recorded = frat_tracer.emitted_events();
            REQUIRE(recorded[0u].kind == tracer::frat::event_kind::add_derived);
            REQUIRE(recorded[0u].ref_offset == ref_10.offset());
            REQUIRE(recorded[0u].clause_id_value == 21u);
            REQUIRE(recorded[0u].literals.size() == 3u);
            REQUIRE(recorded[0u].literals[0u] == 1);
            REQUIRE(recorded[0u].literals[1u] == -2);
            REQUIRE(recorded[0u].literals[2u] == 3);
            REQUIRE(recorded[0u].antecedent_id_values.size() == 2u);
            REQUIRE(recorded[0u].antecedent_id_values[0u] == 5u);
            REQUIRE(recorded[0u].antecedent_id_values[1u] == 7u);

            frat_tracer.finalize();
            frat_tracer.finalize();
            REQUIRE(frat_tracer.emitted_count() == 2u);
            frat_tracer.reset();
            REQUIRE_FALSE(frat_tracer.finalized());
            REQUIRE(frat_tracer.emitted_count() == 0u);
        }

        SECTION("lidrup tracer records epoch-scoped proof events")
        {
            tracer::lidrup lidrup_tracer;
            const cdcl::clause::ref_t ref_12 {12u};

            lidrup_tracer.add_original(ref_12);
            lidrup_tracer.add_derived(ref_12);
            lidrup_tracer.delete_clause(ref_12);
            lidrup_tracer.shrink_clause(ref_12);
            lidrup_tracer.finalize();

            REQUIRE(lidrup_tracer.finalized());
            REQUIRE(lidrup_tracer.current_epoch() == 1u);
            REQUIRE(lidrup_tracer.emitted_count() == 5u);

            const auto& recorded = lidrup_tracer.emitted_events();
            REQUIRE(recorded[0u].kind == tracer::lidrup::event_kind::add_original);
            REQUIRE(recorded[0u].epoch == 0u);
            REQUIRE(recorded[1u].kind == tracer::lidrup::event_kind::add_derived);
            REQUIRE(recorded[2u].kind == tracer::lidrup::event_kind::delete_clause);
            REQUIRE(recorded[3u].kind == tracer::lidrup::event_kind::shrink_clause);
            REQUIRE(recorded[4u].kind == tracer::lidrup::event_kind::finalize);
        }

        SECTION("veripb tracer records the emitted proof events")
        {
            tracer::veripb veripb_tracer;
            const cdcl::clause::ref_t ref_14 {14u};

            veripb_tracer.add_original(ref_14);
            veripb_tracer.add_derived(ref_14);
            veripb_tracer.delete_clause(ref_14);
            veripb_tracer.shrink_clause(ref_14);
            veripb_tracer.finalize();

            REQUIRE(veripb_tracer.finalized());
            REQUIRE(veripb_tracer.emitted_count() == 5u);

            const auto& recorded = veripb_tracer.emitted_events();
            REQUIRE(recorded[0u].kind == tracer::veripb::event_kind::add_original);
            REQUIRE(recorded[1u].kind == tracer::veripb::event_kind::add_derived);
            REQUIRE(recorded[2u].kind == tracer::veripb::event_kind::delete_clause);
            REQUIRE(recorded[3u].kind == tracer::veripb::event_kind::shrink_clause);
            REQUIRE(recorded[4u].kind == tracer::veripb::event_kind::finalize);
        }

        // removed std::cout: "proof manager test passed\n";
    }

} // namespace
