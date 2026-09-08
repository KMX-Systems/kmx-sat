/// @file library/src/kmx/sat/proof/tracer/lrat.cpp
/// @brief Out-of-line definitions declared by kmx/sat/proof/tracer/lrat.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/proof/tracer/lrat.hpp>

namespace kmx::sat::proof::tracer
{
    void lrat::on_event(const proof::proof_event& event) noexcept
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
        std::transform(event.antecedent_ids.begin(), event.antecedent_ids.end(), std::back_inserter(record.antecedent_id_values),
                       [](const auto antecedent) { return antecedent.value(); });
        emitted_events_.push_back(record);
    }
}
