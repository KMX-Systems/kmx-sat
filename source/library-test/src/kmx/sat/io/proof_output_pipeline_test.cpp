#include <catch2/catch_test_macros.hpp>

#include <kmx/sat/io/proof_output_pipeline.hpp>
#include <kmx/sat/proof/event_stream.hpp>

namespace kmx::sat::io
{
    TEST_CASE("proof output pipeline tracks submissions and backpressure", "[sat]")
    {
        proof_output_pipeline pipeline;

        REQUIRE(pipeline.submitted_count() == 0u);

        pipeline.start();
        pipeline.submit();
        REQUIRE(pipeline.submitted_count() == 1u);

        proof::event_stream stream;
        stream.push_event({proof::event_kind::add_original});
        pipeline.submit(stream);
        REQUIRE(pipeline.submitted_count() == 3u);

        pipeline.set_backpressure_policy();
        proof::event_stream second_stream;
        second_stream.push_event({proof::event_kind::conclusion});
        pipeline.submit(second_stream);
        REQUIRE(pipeline.submitted_count() == 6u);

        pipeline.flush();
        REQUIRE(pipeline.submitted_count() == 6u);

        pipeline.stop();
        pipeline.submit(stream);
        REQUIRE(pipeline.submitted_count() == 6u);
    }
}
