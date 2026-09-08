#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>

#include <kmx/sat/io/writer/kmx_aio_proof.hpp>

namespace kmx::sat::io::writer
{
    TEST_CASE("kmx aio proof writer", "[sat]")
    {
        kmx_aio_proof writer;
        std::array<std::byte, 2u> bytes {std::byte {1}, std::byte {2}};

        writer.open_sink();
        writer.submit_buffer(bytes);
        writer.await_flush();
        writer.close_sink();

        REQUIRE(writer.opened());
        REQUIRE(writer.submitted_count() == 1u);
        REQUIRE(writer.flushed());
        REQUIRE(writer.closed());
    }
}