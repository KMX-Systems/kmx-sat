/// @file library/src/kmx/sat/proof/tracer/frat.cpp
/// @brief Out-of-line definitions declared by kmx/sat/proof/tracer/frat.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/proof/tracer/frat.hpp>

namespace kmx::sat::proof::tracer
{
    void frat::on_event(const proof::proof_event& event) noexcept
    {
        event_kind kind {};
        switch (event.kind)
        {
            case proof::event_kind::add_original:
                kind = event_kind::add_original;
                break;
            case proof::event_kind::add_derived:
                kind = event_kind::add_derived;
                break;
            case proof::event_kind::delete_clause:
                kind = event_kind::delete_clause;
                break;
            case proof::event_kind::shrink_clause:
                kind = event_kind::shrink_clause;
                break;
            case proof::event_kind::conclusion:
                finalize();
                return;
        }

        emitted_event record {};
        record.kind = kind;
        record.ref_offset = event.clause_ref.offset();
        record.clause_id_value = event.clause_id.value();
        record.literals = event.literals;
        record.antecedent_id_values.reserve(event.antecedent_ids.size());
        for (const auto antecedent: event.antecedent_ids)
            record.antecedent_id_values.push_back(antecedent.value());
        emitted_events_.push_back(record);
    }

    void frat::finalize() noexcept
    {
        if (finalized_)
            return;
        emitted_events_.push_back({event_kind::finalize, 0u});
        finalized_ = true;
    }
}
