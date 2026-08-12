/// @file inc/kmx/sat/cdcl/external_frontend.hpp
/// @brief The External-inspired layer that isolates external/internal mapping and incremental semantics.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <cstdint>
    #include <span>
    #include <vector>
#endif
#include <kmx/sat/cdcl/failed_core_extractor.hpp>
#include <kmx/sat/cdcl/model_reconstructor.hpp>
#include <kmx/sat/cdcl/variable_mapper.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/model_view.hpp>
#include <kmx/sat/solve_request.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    /// @brief The External-inspired layer that isolates external/internal mapping and incremental semantics.
    ///
    /// Modeled on CaDiCaL's `External` class, this type is the boundary between the public `solver` facade and the
    /// internal `solver_core`: every literal a caller adds or queries passes through `import_external_literal`/
    /// `export_internal_literal`, every frozen/melted variable request is recorded here before `variable_mapper`
    /// applies it, and assumptions pushed by the caller are staged here (backed by `assumption_store`) rather than
    /// merged into the permanent clause database (`constraint_store`). `prepare_solve_request` assembles the
    /// validated per-episode input, `capture_failed_core` and `build_model_view` retrieve the two possible outcomes
    /// through `failed_core_extractor` and `model_reconstructor` respectively, and `apply_compaction_mapping`
    /// propagates a `compaction_service` variable permutation into the external mapping so external variable
    /// identities remain stable across BVE/BCE-driven compaction.
    /// @note Internal-only variables introduced by `factorizer` or `gate_extractor` are allocated by
    /// `variable_mapper::ensure_external_variable` from a range strictly disjoint from external variable ids, and are
    /// filtered out of every model this class builds.
    class external_frontend final
    {
    public:
        /// @brief Constructs an external frontend with empty mapping and assumption state.
        /// @throws None (noexcept).
        external_frontend() noexcept = default;

        explicit external_frontend(variable_mapper& mapper) noexcept: mapper_ {&mapper} {}

        /// @brief Translates one caller-supplied (external) literal into its internal representation.
        /// @param lit External literal as supplied through the public `solver` surface.
        /// @return Internal literal usable by `solver_core` and its collaborators.
        /// @throws None (noexcept).
        literal import_external_literal(const literal lit) noexcept
        {
            if (mapper_ != nullptr)
            {
                const variable internal = mapper_->ensure_external_variable(lit.variable_of());
                return literal {internal, lit.is_negated()};
            }
            return lit;
        }

        /// @brief Translates one internal literal back into its externally visible representation.
        /// @param lit Internal literal produced by the CDCL core.
        /// @return External literal safe to expose to the caller (for example through `model_view`).
        /// @throws None (noexcept).
        literal export_internal_literal(const literal lit) noexcept
        {
            if (mapper_ != nullptr)
            {
                return mapper_->to_external_literal(lit);
            }
            return lit;
        }

        /// @brief Marks a variable as frozen, preventing it from being eliminated by simplification passes so its
        /// external identity and value remain queryable after `solve`.
        /// @param var External variable to freeze.
        /// @throws None (noexcept).
        void freeze_variable(const variable var) noexcept
        {
            if (mapper_ != nullptr)
            {
                mapper_->mark_inactive(var);
            }
        }

        /// @brief Releases a previously frozen variable, allowing simplification passes to eliminate it again.
        /// @param var External variable to melt.
        /// @throws None (noexcept).
        void melt_variable(const variable var) noexcept
        {
            if (mapper_ != nullptr)
            {
                mapper_->mark_active(var);
            }
        }

        /// @brief Stages one assumption literal for the next solve episode, kept separate from the clause database.
        /// @param lit Assumption literal supplied by the caller.
        /// @throws None (noexcept).
        void push_assumption(const literal lit) noexcept { assumptions_.push_back(import_external_literal(lit)); }

        /// @brief Clears all staged assumptions, for example between unrelated incremental solve calls.
        /// @throws None (noexcept).
        void clear_assumptions() noexcept { assumptions_.clear(); }

        /// @brief Stages one normalized clause for the next replay/materialization step.
        /// @param clause Clause literals supplied by a fixture or other external source.
        /// @throws None (noexcept).
        void push_clause(const std::span<const literal> clause) noexcept
        {
            auto& stored_clause = clauses_.emplace_back();
            stored_clause.reserve(clause.size());
            for (const auto lit: clause)
            {
                stored_clause.push_back(import_external_literal(lit));
            }
        }

        /// @brief Clears all staged clauses.
        /// @throws None (noexcept).
        void clear_clauses() noexcept { clauses_.clear(); }

        /// @brief Returns whether any assumptions are currently staged for the next episode.
        /// @return True when at least one assumption literal is pending.
        /// @throws None (noexcept).
        bool has_pending_assumptions() const noexcept { return !assumptions_.empty(); }

        /// @brief Assembles the validated `solve_request` payload for the upcoming episode from the currently staged
        /// assumptions and mapping state.
        /// @throws None (noexcept).
        void prepare_solve_request() noexcept
        {
            prepare_request_ = solve_request {};
            prepare_request_.assumptions = assumptions_;
            prepare_request_.decision_limit = 0u;
            prepare_request_.conflict_limit = 0u;
            prepare_request_.enabled_pass_mask = 0u;
            prepare_request_.strict_mode = false;
        }

        /// @brief Applies a fully formed solve-request payload to the frontend's prepared request state.
        /// @param request Request payload to expose for the next solve episode.
        /// @throws None (noexcept).
        void apply_prepared_request(const solve_request& request) noexcept
        {
            prepare_request_ = request;
            prepare_request_.assumptions = assumptions_;
        }

        /// @brief Retrieves the failed-assumptions core after an unsatisfiable episode, delegating to
        /// `failed_core_extractor`.
        /// @throws None (noexcept).
        void capture_failed_core() noexcept { failed_core_ = failed_core_extractor_.build_failed_core(); }

        /// @brief Builds the externally visible model after a satisfiable episode, delegating to
        /// `model_reconstructor` and filtering out internal-only variables.
        /// @throws None (noexcept).
        void build_model_view() noexcept
        {
            model_reconstructor_.drop_internal_only_variables();
            model_view_ = model_reconstructor_.reconstruct_full_model();
        }

        /// @brief Applies a variable permutation produced by `compaction_service` to the external mapping so external
        /// variable identity survives internal reindexing.
        /// @throws None (noexcept).
        void apply_compaction_mapping() noexcept
        {
            if (mapper_ != nullptr)
            {
                mapper_->rebuild_after_compaction();
            }
        }

        std::vector<literal> assumptions() const noexcept { return assumptions_; }

        std::span<const std::vector<literal>> clauses() const noexcept { return clauses_; }

        solve_request const& prepared_request() const noexcept { return prepare_request_; }

        failed_core_view failed_core() const noexcept { return failed_core_; }

        model_view current_model_view() const noexcept { return model_view_; }

    private:
        variable_mapper* mapper_ {nullptr};
        std::vector<std::vector<literal>> clauses_ {};
        std::vector<literal> assumptions_ {};
        solve_request prepare_request_ {};
        failed_core_view failed_core_ {};
        model_view model_view_ {};
        failed_core_extractor failed_core_extractor_ {};
        model_reconstructor model_reconstructor_ {};
    };
}
