#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/telemetry/logging_facade.hpp>

namespace kmx::sat::telemetry
{
    TEST_CASE("logging facade stores structured events", "[sat]")
    {
        logging_facade logger;
        const cdcl::clause::ref_t ref {11u};
        const literal lit {variable {7u}, false};

        logger.log_clause(ref);
        logger.log_literal(lit);
        logger.log_gate();
        logger.log_extension();
        logger.log_phase_summary("search");

        REQUIRE(logger.event_count() == 5u);
        REQUIRE(logger.last_event().kind == logging_facade::event_kind::phase_summary);
        REQUIRE(logger.last_event().phase_name == "search");
        REQUIRE(logger.last_event().ref_offset == ref.offset());
        REQUIRE(logger.last_literal().literal_value == lit);
        REQUIRE(logger.last_clause_ref() == ref.offset());

        logging_facade empty_logger;
        REQUIRE(empty_logger.event_count() == 0u);
        REQUIRE(empty_logger.last_clause_ref() == 0u);
        REQUIRE(empty_logger.last_literal().literal_value == literal {});
    }
}
