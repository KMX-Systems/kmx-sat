/// @file inc/kmx/sat/proof/tracer/like.hpp
/// @brief Structural concept capturing the uniform event surface shared by every concrete proof tracer.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <concepts>
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>

namespace kmx::sat::proof::tracer
{
    /// @brief Requires `Tracer` to expose the uniform proof-event surface every concrete tracer implements.
    ///
    /// `drat`, `lrat`, `frat`, `idrup`, `lidrup`, and `veripb` all differ only in how they encode a proof, never in
    /// the shape of the events they receive; this concept names that shared shape structurally instead of through a
    /// base class, matching the architecture's rule that tracers use type erasure without virtual dispatch.
    /// `tracer::variant_t` statically asserts every one of its alternatives satisfies this concept, and any future
    /// tracer format added to that variant must satisfy it as well.
    template <typename Tracer>
    concept like = requires(Tracer& tracer, const cdcl::clause::ref_t ref) {
        { tracer.add_original(ref) } noexcept -> std::same_as<void>;
        { tracer.add_derived(ref) } noexcept -> std::same_as<void>;
        { tracer.delete_clause(ref) } noexcept -> std::same_as<void>;
        { tracer.shrink_clause(ref) } noexcept -> std::same_as<void>;
        { tracer.finalize() } noexcept -> std::same_as<void>;
    };
}
