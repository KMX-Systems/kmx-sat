/// @file inc/kmx/sat/proof/tracer/variant_t.hpp
/// @brief Closed-set tag dispatch alternative to virtual proof-sink dispatch.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <variant>
#endif
#include <kmx/sat/proof/tracer/drat.hpp>
#include <kmx/sat/proof/tracer/frat.hpp>
#include <kmx/sat/proof/tracer/idrup.hpp>
#include <kmx/sat/proof/tracer/lidrup.hpp>
#include <kmx/sat/proof/tracer/like.hpp>
#include <kmx/sat/proof/tracer/lrat.hpp>
#include <kmx/sat/proof/tracer/veripb.hpp>

namespace kmx::sat::proof::tracer
{
    /// @brief Closed-set tag dispatch alternative to virtual proof-sink dispatch.
    ///
    /// `variant_t` is the concrete `std::variant` `tracer::view` forwards to via `std::visit`; being a closed set
    /// over exactly the six baseline formats (`drat`, `lrat`, `frat`, `idrup`, `lidrup`, `veripb`) rather than an
    /// open class hierarchy, dispatch through it compiles to a jump table with no vtable, no RTTI, and no
    /// `dynamic_cast`, matching the architecture's constraint that tracers use type erasure without virtual dispatch.
    using variant_t = std::variant<drat, lrat, frat, idrup, lidrup, veripb>;

    static_assert(like<drat>, "drat must satisfy tracer::like");
    static_assert(like<lrat>, "lrat must satisfy tracer::like");
    static_assert(like<frat>, "frat must satisfy tracer::like");
    static_assert(like<idrup>, "idrup must satisfy tracer::like");
    static_assert(like<lidrup>, "lidrup must satisfy tracer::like");
    static_assert(like<veripb>, "veripb must satisfy tracer::like");
}
