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
            const auto co_fsm_start = adapter.lifecycle_metrics_snapshot();
            REQUIRE(adapter.active() == false);
            REQUIRE(adapter.activation_count() == 0u);
            REQUIRE(adapter.deactivation_count() == 0u);
            REQUIRE(adapter.transition_epoch() == 0u);
            adapter.activate();
            REQUIRE(adapter.active());
            REQUIRE(adapter.activation_count() == 1u);
            REQUIRE(adapter.deactivation_count() == 0u);
            REQUIRE(adapter.transition_epoch() == 1u);
            REQUIRE(adapter.last_activation_epoch() == 1u);
            adapter.activate();
            REQUIRE(adapter.activation_count() == 1u);
            REQUIRE(adapter.transition_epoch() == 1u);
            adapter.deactivate();
            REQUIRE(!adapter.active());
            REQUIRE(adapter.activation_count() == 1u);
            REQUIRE(adapter.deactivation_count() == 1u);
            REQUIRE(adapter.transition_epoch() == 2u);
            adapter.deactivate();
            REQUIRE(adapter.deactivation_count() == 1u);
            REQUIRE(adapter.transition_epoch() == 2u);
            const auto co_fsm_after = adapter.lifecycle_metrics_snapshot();
            REQUIRE(co_fsm_after.active == false);
            REQUIRE(co_fsm_after.activation_count == 1u);
            REQUIRE(co_fsm_after.deactivation_count == 1u);
            REQUIRE(co_fsm_after.transition_epoch == 2u);
            REQUIRE(co_fsm_adapter::lifecycle_monotonic(co_fsm_start, co_fsm_after));

            adapter.reset_lifecycle_metrics();
            const auto co_fsm_reset = adapter.lifecycle_metrics_snapshot();
            REQUIRE(co_fsm_reset.active == false);
            REQUIRE(co_fsm_reset.activation_count == 0u);
            REQUIRE(co_fsm_reset.deactivation_count == 0u);
            REQUIRE(co_fsm_reset.transition_epoch == 0u);
            REQUIRE(co_fsm_reset.last_activation_epoch == 0u);
        }

        SECTION("signal controller tracks stop requests")
        {
            controller::signal signal_controller;
            const auto signal_start = signal_controller.metrics_snapshot();
            REQUIRE(!signal_controller.handlers_installed());
            REQUIRE(signal_controller.install_count() == 0u);
            REQUIRE(!signal_controller.termination_requested());
            REQUIRE(signal_controller.stop_request_count() == 0u);
            REQUIRE(signal_controller.os_signal_request_count() == 0u);
            REQUIRE(signal_controller.clear_count() == 0u);

            signal_controller.notify_os_signal();
            REQUIRE(!signal_controller.termination_requested());
            REQUIRE(signal_controller.stop_request_count() == 0u);
            REQUIRE(signal_controller.os_signal_request_count() == 0u);

            signal_controller.install_handlers();
            REQUIRE(signal_controller.handlers_installed());
            REQUIRE(signal_controller.install_count() == 1u);
            signal_controller.install_handlers();
            REQUIRE(signal_controller.install_count() == 1u);

            signal_controller.notify_os_signal();
            REQUIRE(signal_controller.termination_requested());
            REQUIRE(signal_controller.stop_request_count() == 1u);
            REQUIRE(signal_controller.os_signal_request_count() == 1u);

            signal_controller.clear();
            REQUIRE(!signal_controller.termination_requested());
            REQUIRE(signal_controller.clear_count() == 1u);

            signal_controller.request_stop();
            REQUIRE(signal_controller.termination_requested());
            REQUIRE(signal_controller.stop_request_count() == 2u);

            signal_controller.clear();
            REQUIRE(!signal_controller.termination_requested());
            REQUIRE(signal_controller.clear_count() == 2u);
            const auto signal_after = signal_controller.metrics_snapshot();
            REQUIRE(signal_after.handlers_installed);
            REQUIRE(signal_after.termination_requested == false);
            REQUIRE(signal_after.install_count == 1u);
            REQUIRE(signal_after.stop_request_count == 2u);
            REQUIRE(signal_after.os_signal_request_count == 1u);
            REQUIRE(signal_after.clear_count == 2u);
            REQUIRE(controller::signal::metrics_monotonic(signal_start, signal_after));

            signal_controller.reset_metrics();
            const auto signal_reset = signal_controller.metrics_snapshot();
            REQUIRE(signal_reset.handlers_installed == false);
            REQUIRE(signal_reset.termination_requested == false);
            REQUIRE(signal_reset.install_count == 0u);
            REQUIRE(signal_reset.stop_request_count == 0u);
            REQUIRE(signal_reset.os_signal_request_count == 0u);
            REQUIRE(signal_reset.clear_count == 0u);
        }

        SECTION("parallel preprocess executor tracks deterministic pass execution")
        {
            parallel_preprocess_executor executor;
            const auto executor_start = executor.execution_metrics_snapshot();
            REQUIRE(executor.thread_budget() == 1u);
            REQUIRE(executor.runs() == 0u);
            REQUIRE(executor.merges() == 0u);
            REQUIRE(executor.idle_merge_skip_count() == 0u);

            executor.set_thread_budget(0u);
            REQUIRE(executor.thread_budget() == 1u);
            executor.set_thread_budget(300u);
            REQUIRE(executor.thread_budget() == 256u);
            executor.set_thread_budget(4u);
            REQUIRE(executor.thread_budget() == 4u);

            executor.set_work_item_count(13u);
            REQUIRE(executor.configured_work_item_count() == 13u);

            executor.run_parallel_pass();
            REQUIRE(executor.last_parallel_chunk_size() == 4u);
            REQUIRE(executor.total_processed_work_item_count() == 13u);
            executor.merge_deterministic_result();
            executor.merge_deterministic_result();

            REQUIRE(executor.thread_budget() == 4u);
            REQUIRE(executor.runs() == 1u);
            REQUIRE(executor.merges() == 1u);
            REQUIRE(executor.idle_merge_skip_count() == 1u);
            const auto executor_after = executor.execution_metrics_snapshot();
            REQUIRE(executor_after.thread_budget == 4u);
            REQUIRE(executor_after.runs == 1u);
            REQUIRE(executor_after.merges == 1u);
            REQUIRE(executor_after.idle_merge_skip_count == 1u);
            REQUIRE(executor_after.last_parallel_chunk_size == 4u);
            REQUIRE(executor_after.total_processed_work_item_count == 13u);
            REQUIRE(parallel_preprocess_executor::execution_metrics_monotonic(executor_start, executor_after));

            executor.reset_execution_metrics();
            const auto executor_reset = executor.execution_metrics_snapshot();
            REQUIRE(executor_reset.thread_budget == 1u);
            REQUIRE(executor_reset.runs == 0u);
            REQUIRE(executor_reset.merges == 0u);
            REQUIRE(executor_reset.idle_merge_skip_count == 0u);
            REQUIRE(executor_reset.configured_work_item_count == 0u);
            REQUIRE(executor_reset.last_parallel_chunk_size == 0u);
            REQUIRE(executor_reset.total_processed_work_item_count == 0u);
        }

        SECTION("portfolio controller tracks launch and cancellation state")
        {
            controller::portfolio portfolio_controller;
            const auto portfolio_start = portfolio_controller.lifecycle_metrics_snapshot();
            REQUIRE(!portfolio_controller.launched());
            REQUIRE(!portfolio_controller.cancelled());
            REQUIRE(!portfolio_controller.collected());
            REQUIRE(!portfolio_controller.termination_requested());
            REQUIRE(portfolio_controller.strategy_budget() == 1u);
            REQUIRE(portfolio_controller.launch_count() == 0u);
            REQUIRE(portfolio_controller.launch_epoch() == 0u);
            REQUIRE(portfolio_controller.cancel_count() == 0u);
            REQUIRE(portfolio_controller.collect_count() == 0u);

            portfolio_controller.set_strategy_budget(0u);
            REQUIRE(portfolio_controller.strategy_budget() == 1u);
            portfolio_controller.set_strategy_budget(70u);
            REQUIRE(portfolio_controller.strategy_budget() == 64u);
            portfolio_controller.set_strategy_budget(6u);
            REQUIRE(portfolio_controller.strategy_budget() == 6u);

            portfolio_controller.cancel_others_on_result();
            portfolio_controller.collect_winner();
            REQUIRE(portfolio_controller.cancel_count() == 0u);
            REQUIRE(portfolio_controller.collect_count() == 0u);

            portfolio_controller.launch_strategies();
            REQUIRE(portfolio_controller.launched());
            REQUIRE(portfolio_controller.launch_count() == 1u);
            REQUIRE(portfolio_controller.launch_epoch() == 1u);
            REQUIRE(!portfolio_controller.termination_requested());

            portfolio_controller.cancel_others_on_result();
            REQUIRE(portfolio_controller.cancelled());
            REQUIRE(portfolio_controller.cancel_count() == 1u);
            REQUIRE(portfolio_controller.termination_requested());
            portfolio_controller.cancel_others_on_result();
            REQUIRE(portfolio_controller.cancel_count() == 1u);

            portfolio_controller.collect_winner();
            REQUIRE(portfolio_controller.collected());
            REQUIRE(portfolio_controller.collect_count() == 1u);
            portfolio_controller.collect_winner();
            REQUIRE(portfolio_controller.collect_count() == 1u);

            portfolio_controller.launch_strategies();
            REQUIRE(portfolio_controller.launch_count() == 2u);
            REQUIRE(portfolio_controller.launch_epoch() == 2u);
            REQUIRE(!portfolio_controller.cancelled());
            REQUIRE(!portfolio_controller.collected());
            REQUIRE(!portfolio_controller.termination_requested());
            const auto portfolio_after = portfolio_controller.lifecycle_metrics_snapshot();
            REQUIRE(portfolio_after.launch_count == 2u);
            REQUIRE(portfolio_after.launch_epoch == 2u);
            REQUIRE(portfolio_after.cancel_count == 1u);
            REQUIRE(portfolio_after.collect_count == 1u);
            REQUIRE(portfolio_after.strategy_budget == 6u);
            REQUIRE(controller::portfolio::lifecycle_monotonic(portfolio_start, portfolio_after));

            portfolio_controller.reset_lifecycle_metrics();
            const auto portfolio_reset = portfolio_controller.lifecycle_metrics_snapshot();
            REQUIRE(portfolio_reset.launched == false);
            REQUIRE(portfolio_reset.cancelled == false);
            REQUIRE(portfolio_reset.collected == false);
            REQUIRE(portfolio_reset.termination_requested == false);
            REQUIRE(portfolio_reset.launch_count == 0u);
            REQUIRE(portfolio_reset.launch_epoch == 0u);
            REQUIRE(portfolio_reset.cancel_count == 0u);
            REQUIRE(portfolio_reset.collect_count == 0u);
            REQUIRE(portfolio_reset.strategy_budget == 1u);
        }

        SECTION("shared clause exchange tracks publish and drain state")
        {
            shared_clause_exchange exchange;
            const cdcl::clause::ref_t ref {9u};
            const cdcl::clause::ref_t ref_other {10u};
            const cdcl::clause::ref_t ref_third {11u};
            const auto exchange_start = exchange.exchange_metrics_snapshot();

            exchange.set_max_pending(0u);
            REQUIRE(exchange.max_pending() == 1u);
            exchange.set_max_pending(2u);
            REQUIRE(exchange.max_pending() == 2u);

            REQUIRE(!exchange.has_pending());
            REQUIRE(exchange.pending_count() == 0u);
            REQUIRE(exchange.publish_attempt_count() == 0u);
            REQUIRE(exchange.dropped_invalid_ref_count() == 0u);
            REQUIRE(exchange.dropped_duplicate_ref_count() == 0u);
            REQUIRE(exchange.truncated_by_policy_count() == 0u);
            REQUIRE(exchange.policy_apply_count() == 0u);
            exchange.publish_clause(ref);
            exchange.publish_clause(ref);
            exchange.publish_clause(cdcl::clause::ref_t {});
            exchange.publish_clause(ref_other);
            exchange.publish_clause(ref_third);
            REQUIRE(exchange.has_pending());
            REQUIRE(exchange.pending_count() == 3u);
            REQUIRE(exchange.publish_attempt_count() == 5u);
            REQUIRE(exchange.dropped_invalid_ref_count() == 1u);
            REQUIRE(exchange.dropped_duplicate_ref_count() == 1u);

            exchange.apply_exchange_policy();
            REQUIRE(exchange.policy_applied());
            REQUIRE(exchange.pending_count() == 2u);
            REQUIRE(exchange.policy_apply_count() == 1u);
            REQUIRE(exchange.truncated_by_policy_count() == 1u);

            exchange.drain_incoming();
            REQUIRE(exchange.drain_count() == 1u);
            REQUIRE(exchange.last_drain_size() == 2u);
            REQUIRE(exchange.total_drained_refs() == 2u);
            const auto exchange_after = exchange.exchange_metrics_snapshot();
            REQUIRE(exchange_after.has_pending == false);
            REQUIRE(exchange_after.policy_applied);
            REQUIRE(exchange_after.max_pending == 2u);
            REQUIRE(exchange_after.drain_count == 1u);
            REQUIRE(exchange_after.publish_attempt_count == 5u);
            REQUIRE(exchange_after.dropped_invalid_ref_count == 1u);
            REQUIRE(exchange_after.dropped_duplicate_ref_count == 1u);
            REQUIRE(exchange_after.truncated_by_policy_count == 1u);
            REQUIRE(exchange_after.policy_apply_count == 1u);
            REQUIRE(exchange_after.last_drain_size == 2u);
            REQUIRE(exchange_after.total_drained_refs == 2u);
            REQUIRE(shared_clause_exchange::exchange_metrics_monotonic(exchange_start, exchange_after));

            exchange.reset_exchange_state();
            REQUIRE(!exchange.has_pending());
            REQUIRE(exchange.pending_count() == 0u);
            REQUIRE(exchange.drain_count() == 0u);
            REQUIRE(exchange.last_drain_size() == 0u);
            REQUIRE(exchange.total_drained_refs() == 0u);
            REQUIRE(!exchange.policy_applied());
            REQUIRE(exchange.publish_attempt_count() == 0u);
            REQUIRE(exchange.dropped_invalid_ref_count() == 0u);
            REQUIRE(exchange.dropped_duplicate_ref_count() == 0u);
            REQUIRE(exchange.truncated_by_policy_count() == 0u);
            REQUIRE(exchange.policy_apply_count() == 0u);

            exchange.set_max_pending(1u);
            exchange.publish_clause(ref);
            exchange.publish_clause(ref_other);
            exchange.publish_clause(ref_third);
            exchange.apply_exchange_policy();
            REQUIRE(exchange.max_pending() == 1u);
            exchange.reset_exchange_metrics();
            const auto exchange_reset = exchange.exchange_metrics_snapshot();
            REQUIRE(exchange_reset.has_pending == false);
            REQUIRE(exchange_reset.policy_applied == false);
            REQUIRE(exchange_reset.pending_count == 0u);
            REQUIRE(exchange_reset.max_pending == 4u);
            REQUIRE(exchange_reset.drain_count == 0u);
            REQUIRE(exchange_reset.publish_attempt_count == 0u);
            REQUIRE(exchange_reset.dropped_invalid_ref_count == 0u);
            REQUIRE(exchange_reset.dropped_duplicate_ref_count == 0u);
            REQUIRE(exchange_reset.truncated_by_policy_count == 0u);
            REQUIRE(exchange_reset.policy_apply_count == 0u);
            REQUIRE(exchange_reset.last_drain_size == 0u);
            REQUIRE(exchange_reset.total_drained_refs == 0u);
        }
    }
}