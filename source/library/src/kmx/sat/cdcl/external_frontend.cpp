/// @file library/src/kmx/sat/cdcl/external_frontend.cpp
/// @brief Out-of-line definitions declared by kmx/sat/cdcl/external_frontend.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/cdcl/external_frontend.hpp>

namespace kmx::sat::cdcl
{
    literal external_frontend::import_external_literal(const literal lit) noexcept
    {
        if (mapper_ != nullptr)
        {
            const variable internal = mapper_->ensure_external_variable(lit.variable_of());
            return literal {internal, lit.is_negated()};
        }
        return lit;
    }

    literal external_frontend::export_internal_literal(const literal lit) noexcept
    {
        if (mapper_ != nullptr)
            return mapper_->to_external_literal(lit);
        return lit;
    }

    void external_frontend::push_clause(const std::span<const literal> clause) noexcept
    {
        auto& stored_clause = clauses_.emplace_back();
        stored_clause.reserve(clause.size());
        for (const auto lit: clause)
            stored_clause.push_back(import_external_literal(lit));
    }

    void external_frontend::prepare_solve_request() noexcept
    {
        prepare_request_ = solve_request {};
        prepare_request_.assumptions = assumptions_;
        prepare_request_.decision_limit = 0u;
        prepare_request_.conflict_limit = 0u;
        prepare_request_.enabled_pass_mask = 0u;
        prepare_request_.strict_mode = false;
    }
}
