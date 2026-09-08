/// @file library/src/kmx/sat/proof/tracer/drat.cpp
/// @brief Out-of-line definitions declared by kmx/sat/proof/tracer/drat.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/proof/tracer/drat.hpp>

namespace kmx::sat::proof::tracer
{
    void drat::on_event(const proof::proof_event& event) noexcept
    {
        switch (event.kind)
        {
            case proof::event_kind::add_original:
                add_original(event.clause_ref);
                break;
            case proof::event_kind::add_derived:
                add_derived(event.clause_ref);
                break;
            case proof::event_kind::delete_clause:
                delete_clause(event.clause_ref);
                break;
            case proof::event_kind::shrink_clause:
                shrink_clause(event.clause_ref);
                break;
            case proof::event_kind::conclusion:
                finalize();
                break;
        }
    }
}
