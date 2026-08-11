#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/proof/event_stream.hpp>

namespace kmx::sat::proof
{
    TEST_CASE("event stream tracks buffered and flushed activity", "[sat]")
    {
        event_stream stream;
        proof_event event {event_kind::add_original};

        stream.push_event(event);
        REQUIRE(stream.buffered_count() == 1u);
        REQUIRE(stream.last_drain_count() == 0u);

        stream.drain();
        REQUIRE(stream.last_drain_count() == 1u);
        REQUIRE(stream.buffered_count() == 0u);

        stream.push_event(event);
        stream.flush_sync();
        REQUIRE(stream.last_flush_count() == 1u);

        stream.push_event(event);
        stream.flush_async();
        REQUIRE(stream.last_flush_count() == 1u);
    }
}
