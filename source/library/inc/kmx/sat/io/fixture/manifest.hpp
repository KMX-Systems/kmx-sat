/// @file inc/kmx/sat/io/fixture/manifest.hpp
/// @brief Reproducibility and provenance for CI and benchmark artifacts.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <string>
    #include <string_view>
#endif

namespace kmx::sat::io::fixture
{
    /// @brief Reproducibility and provenance for CI and benchmark artifacts.
    /// @details
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

        /// @brief Records the unique identifier of this fixture artifact.
        /// @param value Fixture identifier value.
        /// @throws None (noexcept).
        void set_fixture_id(const std::uint64_t value) noexcept { fixture_id_ = value; }

        /// @brief Records a description of where this fixture's data originated.
        /// @param value Source origin description.
        /// @throws None (noexcept).
        void set_source_origin(const std::string_view value) noexcept { source_origin_.assign(value.begin(), value.end()); }

        /// @brief Records the normalization policy applied when this fixture was created.
        /// @param value Normalization profile identifier.
        /// @throws None (noexcept).
        void set_normalization_profile(const std::string_view value) noexcept { normalization_profile_.assign(value.begin(), value.end()); }

        /// @brief Records the solver build fingerprint used to generate this fixture.
        /// @param value Build fingerprint string.
        /// @throws None (noexcept).
        void set_build_fingerprint(const std::string_view value) noexcept { build_fingerprint_.assign(value.begin(), value.end()); }

        /// @brief Records the toolchain fingerprint used to generate this fixture.
        /// @param value Toolchain fingerprint string.
        /// @throws None (noexcept).
        void set_toolchain_fingerprint(const std::string_view value) noexcept { toolchain_fingerprint_.assign(value.begin(), value.end()); }

        /// @brief Returns the unique identifier of this fixture artifact.
        /// @return Fixture identifier value.
        /// @throws None (noexcept).
        std::uint64_t fixture_id() const noexcept { return fixture_id_; }

        /// @brief Returns a description of where this fixture's data originated.
        /// @return Source origin description.
        /// @throws None (noexcept).
        std::string_view source_origin() const noexcept { return source_origin_; }

        /// @brief Returns the normalization policy applied when this fixture was created.
        /// @return Normalization profile identifier.
        /// @throws None (noexcept).
        std::string_view normalization_profile() const noexcept { return normalization_profile_; }

        /// @brief Returns the solver build fingerprint used to generate this fixture.
        /// @return Build fingerprint string.
        /// @throws None (noexcept).
        std::string_view build_fingerprint() const noexcept { return build_fingerprint_; }

        /// @brief Returns the toolchain fingerprint used to generate this fixture.
        /// @return Toolchain fingerprint string.
        /// @throws None (noexcept).
        std::string_view toolchain_fingerprint() const noexcept { return toolchain_fingerprint_; }

        /// @brief Reports whether this manifest carries any provenance information.
        /// @return True when at least one provenance field has been populated.
        /// @throws None (noexcept).
        bool has_provenance() const noexcept
        {
            return fixture_id_ != 0u || !source_origin_.empty() || !normalization_profile_.empty() || !build_fingerprint_.empty() ||
                   !toolchain_fingerprint_.empty();
        }

    private:
        std::uint64_t fixture_id_ {};
        std::string source_origin_ {};
        std::string normalization_profile_ {};
        std::string build_fingerprint_ {};
        std::string toolchain_fingerprint_ {};
    };
}
