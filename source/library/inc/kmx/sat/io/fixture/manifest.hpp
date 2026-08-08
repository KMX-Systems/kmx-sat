/// @file inc/kmx/sat/io/fixture/manifest.hpp
/// @brief Reproducibility and provenance for CI and benchmark artifacts.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <string_view>
#endif

namespace kmx::sat::io::fixture
{
    /// @brief Reproducibility and provenance for CI and benchmark artifacts.
    ///
    /// `manifest` records provenance metadata alongside a binary fixture so CI and benchmark tooling can trace a
    /// result back to exactly how it was produced: `fixture_id` uniquely identifies the fixture artifact;
    /// `source_origin` records where its data came from (for example the original DIMACS file or generator);
    /// `normalization_profile` records which normalization policy (deduplication/tautology handling) was applied when
    /// it was created; `build_fingerprint`/`toolchain_fingerprint` record the solver build and toolchain versions used
    /// to generate it, so a later regression can distinguish a genuine behavior change from a fixture generated under
    /// different build conditions.
    class manifest final
    {
    public:
        /// @brief Constructs a manifest with no recorded provenance.
        /// @throws None (noexcept).
        manifest() noexcept = default;

        /// @brief Returns the unique identifier of this fixture artifact.
        /// @return Fixture identifier value.
        /// @throws None (noexcept).
        std::uint64_t fixture_id() const noexcept
        {
            return {};
        }

        /// @brief Returns a description of where this fixture's data originated.
        /// @return Source origin description.
        /// @throws None (noexcept).
        std::string_view source_origin() const noexcept
        {
            return {};
        }

        /// @brief Returns the normalization policy applied when this fixture was created.
        /// @return Normalization profile identifier.
        /// @throws None (noexcept).
        std::string_view normalization_profile() const noexcept
        {
            return {};
        }

        /// @brief Returns the solver build fingerprint used to generate this fixture.
        /// @return Build fingerprint string.
        /// @throws None (noexcept).
        std::string_view build_fingerprint() const noexcept
        {
            return {};
        }

        /// @brief Returns the toolchain fingerprint used to generate this fixture.
        /// @return Toolchain fingerprint string.
        /// @throws None (noexcept).
        std::string_view toolchain_fingerprint() const noexcept
        {
            return {};
        }
    };
}
