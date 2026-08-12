#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/runtime/co_fsm_adapter.hpp>
#include <kmx/sat/runtime/controller/portfolio.hpp>
#include <kmx/sat/runtime/controller/signal.hpp>
#include <kmx/sat/runtime/parallel_preprocess_executor.hpp>
#include <kmx/sat/runtime/shared_clause_exchange.hpp>

namespace kmx::sat::runtime
{
    TEST_CASE("runtime placeholders", "[sat]")
    {
        SECTION("co fsm adapter is a documented placeholder")
        {
            co_fsm_adapter adapter;
            REQUIRE(adapter.active() == false);
            adapter.activate();
            REQUIRE(adapter.active());
            adapter.deactivate();
            REQUIRE(!adapter.active());
        }

        SECTION("signal controller tracks stop requests")
        {
            controller::signal signal_controller;
            REQUIRE(!signal_controller.termination_requested());

            signal_controller.install_handlers();
            signal_controller.request_stop();
            REQUIRE(signal_controller.termination_requested());

            signal_controller.clear();
            REQUIRE(!signal_controller.termination_requested());
        }

        SECTION("parallel preprocess executor tracks deterministic pass execution")
        {
            parallel_preprocess_executor executor;
            REQUIRE(executor.thread_budget() == 1u);

            executor.run_parallel_pass();
            executor.merge_deterministic_result();

            REQUIRE(executor.thread_budget() == 1u);
            REQUIRE(executor.runs() == 1u);
            REQUIRE(executor.merges() == 1u);
        }

        SECTION("portfolio controller tracks launch and cancellation state")
        {
            controller::portfolio portfolio_controller;
            REQUIRE(!portfolio_controller.launched());
            REQUIRE(!portfolio_controller.cancelled());
            REQUIRE(!portfolio_controller.collected());

            portfolio_controller.launch_strategies();
            REQUIRE(portfolio_controller.launched());

            portfolio_controller.cancel_others_on_result();
            REQUIRE(portfolio_controller.cancelled());

            portfolio_controller.collect_winner();
            REQUIRE(portfolio_controller.collected());
        }

        SECTION("shared clause exchange tracks publish and drain state")
        {
            shared_clause_exchange exchange;
            const cdcl::clause::ref_t ref {9u};

            REQUIRE(!exchange.has_pending());
            exchange.publish_clause(ref);
            REQUIRE(exchange.has_pending());
            exchange.apply_exchange_policy();
            exchange.drain_incoming();
            REQUIRE(exchange.drain_count() == 1u);
            exchange.reset_exchange_state();
            REQUIRE(!exchange.has_pending());
        }
    }
}