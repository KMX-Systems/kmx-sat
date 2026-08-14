/// @file inc/kmx/sat/proof/tracer/view.hpp
/// @brief Type-erased wrapper for proof sinks without virtual dispatch in the hot path.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <optional>
    #include <utility>
#endif
#include <kmx/sat/cdcl/clause/ref_t.hpp>
#include <kmx/sat/proof/event_stream.hpp>
#include <kmx/sat/proof/tracer/variant_t.hpp>

namespace kmx::sat::proof::tracer
{
    /// @brief Type-erased wrapper for proof sinks without virtual dispatch in the hot path, implemented as a closed
    /// std::variant over the concrete tracer types with std::visit-based tag dispatch; no vtable, no RTTI.
    /// @details
    /// `tracer::view` gives `proof::proof_manager` one uniform call surface (`add_original`/`add_derived`/
    /// `delete_clause`/`shrink_clause`/`finalize`) over whichever concrete tracer (`tracer::drat`, `tracer::lrat`,
    /// `tracer::frat`, `tracer::idrup`, `tracer::lidrup`, or `tracer::veripb`) is actually active, without
    /// introducing a class hierarchy or `virtual` dispatch: internally it forwards to `tracer::variant_t`
    /// (`std::variant` over the concrete types) using `std::visit`-based tag dispatch. This matches the
    /// architecture's rule that type erasure is permitted only for tracers/cold sinks and must never use
    /// `dynamic_cast`/vtables/RTTI, keeping dispatch cost bounded and predictable even off the CDCL hot path.
    /// @note The call surface mirrored here is named structurally by the `tracer::like` concept, which every
    /// alternative of `tracer::variant_t` is statically asserted to satisfy.
    class view final
    {
    public:
        /// @brief Constructs an empty tracer view bound to no concrete tracer.
        /// @throws None (noexcept).
        view() noexcept = default;

        /// @brief Constructs a view wrapping a concrete tracer.
        /// @param tracer Concrete tracer to wrap.
        /// @throws None (noexcept).
        explicit view(variant_t tracer) noexcept: variant_ {std::move(tracer)} {}

        /// @brief Constructs a view wrapping a concrete DRAT tracer.
        /// @param tracer Concrete DRAT tracer.
        /// @throws None (noexcept).
        explicit view(drat tracer) noexcept: variant_ {std::move(tracer)} {}

        /// @brief Constructs a view wrapping a concrete LRAT tracer.
        /// @param tracer Concrete LRAT tracer.
        /// @throws None (noexcept).
        explicit view(lrat tracer) noexcept: variant_ {std::move(tracer)} {}

        /// @brief Constructs a view wrapping a concrete FRAT tracer.
        /// @param tracer Concrete FRAT tracer.
        /// @throws None (noexcept).
        explicit view(frat tracer) noexcept: variant_ {std::move(tracer)} {}

        /// @brief Constructs a view wrapping a concrete IDRUP tracer.
        /// @param tracer Concrete IDRUP tracer.
        /// @throws None (noexcept).
        explicit view(idrup tracer) noexcept: variant_ {std::move(tracer)} {}

        /// @brief Constructs a view wrapping a concrete LIDRUP tracer.
        /// @param tracer Concrete LIDRUP tracer.
        /// @throws None (noexcept).
        explicit view(lidrup tracer) noexcept: variant_ {std::move(tracer)} {}

        /// @brief Constructs a view wrapping a concrete VERIPB tracer.
        /// @param tracer Concrete VERIPB tracer.
        /// @throws None (noexcept).
        explicit view(veripb tracer) noexcept: variant_ {std::move(tracer)} {}

        /// @brief Forwards an original-clause-added event to the active concrete tracer.
        /// @param ref Reference to the newly added original clause.
        /// @throws None (noexcept).
        void add_original(const cdcl::clause::ref_t ref) noexcept
        {
            if (!variant_)
                return;
            std::visit([&](auto& tracer) noexcept { tracer.add_original(ref); }, *variant_);
        }

        /// @brief Forwards a derived-clause-added event to the active concrete tracer.
        /// @param ref Reference to the newly derived clause.
        /// @throws None (noexcept).
        void add_derived(const cdcl::clause::ref_t ref) noexcept
        {
            if (!variant_)
                return;
            std::visit([&](auto& tracer) noexcept { tracer.add_derived(ref); }, *variant_);
        }

        /// @brief Forwards a clause-deleted event to the active concrete tracer.
        /// @param ref Reference to the deleted clause.
        /// @throws None (noexcept).
        void delete_clause(const cdcl::clause::ref_t ref) noexcept
        {
            if (!variant_)
                return;
            std::visit([&](auto& tracer) noexcept { tracer.delete_clause(ref); }, *variant_);
        }

        /// @brief Forwards a clause-shrunk event to the active concrete tracer.
        /// @param ref Reference to the shrunk clause.
        /// @throws None (noexcept).
        void shrink_clause(const cdcl::clause::ref_t ref) noexcept
        {
            if (!variant_)
                return;
            std::visit([&](auto& tracer) noexcept { tracer.shrink_clause(ref); }, *variant_);
        }

        /// @brief Forwards a fully-populated proof event payload to the active concrete tracer.
        /// @param event Proof event payload containing kind, ids, literals, and antecedents.
        /// @throws None (noexcept).
        void on_event(const proof::proof_event& event) noexcept
        {
            if (!variant_)
                return;
            std::visit([&](auto& tracer) noexcept { tracer.on_event(event); }, *variant_);
        }

        /// @brief Forwards the proof-finalization event to the active concrete tracer.
        /// @throws None (noexcept).
        void finalize() noexcept
        {
            if (!variant_)
                return;
            std::visit([](auto& tracer) noexcept { tracer.finalize(); }, *variant_);
        }

        /// @brief Returns the concrete proof format name represented by this view.
        /// @return Format identifier, or empty when no tracer is bound.
        std::string_view format_name() const noexcept
        {
            if (!variant_)
                return {};

            return std::visit(
                [](const auto& tracer) noexcept -> std::string_view
                {
                    using tracer_t = std::decay_t<decltype(tracer)>;
                    if constexpr (std::same_as<tracer_t, drat>)
                        return "drat";
                    else if constexpr (std::same_as<tracer_t, lrat>)
                        return "lrat";
                    else if constexpr (std::same_as<tracer_t, frat>)
                        return "frat";
                    else if constexpr (std::same_as<tracer_t, idrup>)
                        return "idrup";
                    else if constexpr (std::same_as<tracer_t, lidrup>)
                        return "lidrup";
                    else
                        return "veripb";
                },
                *variant_);
        }

    private:
        std::optional<variant_t> variant_ {};
    };
}
