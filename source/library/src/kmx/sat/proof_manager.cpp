/// @file library/src/kmx/sat/proof_manager.cpp
/// @brief Out-of-line definitions declared by kmx/sat/proof_manager.hpp.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/sat/proof_manager.hpp>

namespace kmx::sat
{
    void proof_manager::enable_format(const proof::format_id id) noexcept
    {
        if (has_enabled_format(id))
            return;

        enabled_format_mask_ |= format_bit(id);
        event_buffering_enabled_ = true;
        switch (id)
        {
            case proof::format_id::drat:
                enabled_tracers_.emplace_back(proof::tracer::view {proof::tracer::drat {}});
                break;
            case proof::format_id::lrat:
                enabled_tracers_.emplace_back(proof::tracer::view {proof::tracer::lrat {}});
                break;
            case proof::format_id::frat:
                enabled_tracers_.emplace_back(proof::tracer::view {proof::tracer::frat {}});
                break;
            case proof::format_id::idrup:
                enabled_tracers_.emplace_back(proof::tracer::view {proof::tracer::idrup {}});
                break;
            case proof::format_id::lidrup:
                enabled_tracers_.emplace_back(proof::tracer::view {proof::tracer::lidrup {}});
                break;
            case proof::format_id::veripb:
                enabled_tracers_.emplace_back(proof::tracer::view {proof::tracer::veripb {}});
                break;
        }
    }

    void proof_manager::enable_checker(const proof::checker_id id) noexcept
    {
        switch (id)
        {
            case proof::checker_id::online:
                online_checker_enabled_ = true;
                break;
            case proof::checker_id::lrat:
                lrat_checker_enabled_ = true;
                break;
        }
        event_buffering_enabled_ = true;
    }

    void proof_manager::register_tracer(const proof::tracer::view& sink) noexcept
    {
        if (const auto format = sink.format(); format.has_value())
            enabled_format_mask_ |= format_bit(*format);
        registered_tracers_.push_back(sink);
        event_buffering_enabled_ = true;
    }

    void proof_manager::on_add_derived(const cdcl::clause::ref_t ref, const std::span<const literal> literals,
                                       const std::span<const proof::clause::id> antecedents) noexcept
    {
        dispatch_event(proof::event_kind::add_derived, ref, literals, antecedents);
        if (!antecedents.empty())
            record_checker_antecedents(stable_id_for_clause(ref), antecedents);
    }

    void proof_manager::on_clause_relocated(const cdcl::clause::ref_t old_ref, const cdcl::clause::ref_t new_ref) noexcept
    {
        id_allocator_.preserve_on_relocation(old_ref, new_ref);
        if (online_checker_enabled_)
            online_checker_.on_relocate(old_ref, new_ref);
    }

    [[nodiscard]] bool proof_manager::validate_checkers() const noexcept
    {
        if (online_checker_enabled_ && !online_checker_.validate_conclusion())
            return false;
        if (lrat_checker_enabled_ && lrat_checker_.has_recorded() && !lrat_checker_.check_chain())
            return false;
        return true;
    }

    proof::clause::id proof_manager::resolve_clause_id(const proof::event_kind kind, const cdcl::clause::ref_t ref) noexcept
    {
        switch (kind)
        {
            case proof::event_kind::add_original:
            case proof::event_kind::add_derived:
                return id_allocator_.allocate_for_new_clause(ref);
            case proof::event_kind::delete_clause:
            case proof::event_kind::shrink_clause:
                return id_allocator_.id_for_clause_ref(ref);
            case proof::event_kind::conclusion:
                return {};
        }

        return {};
    }

    void proof_manager::dispatch_event(const proof::event_kind kind, const cdcl::clause::ref_t ref, const std::span<const literal> literals,
                                       const std::span<const proof::clause::id> antecedents) noexcept
    {
        // With nothing attached there is nothing to record: the clause-id table alone was a third of the
        // instructions a proof-free run spent on reading and preprocessing a 114k-clause formula. A consumer
        // attached later receives ids for every clause alive at that point through `adopt_clause`.
        if (!has_active_consumers())
            return;
        // Reuse `last_event_`'s buffers so a solve with no proof consumer performs no per-event allocation.
        auto& event = last_event_;
        event.kind = kind;
        event.clause_ref = ref;
        event.clause_id = resolve_clause_id(kind, ref);
        event.finalized = kind == proof::event_kind::conclusion;
        event.literals.clear();
        event.antecedent_ids.clear();
        const auto is_add_or_shrink = (kind == proof::event_kind::add_original) || (kind == proof::event_kind::add_derived) ||
                                      (kind == proof::event_kind::shrink_clause);
        if (is_add_or_shrink)
        {
            event.literals.reserve(literals.size());
            for (const auto lit: literals)
                event.literals.push_back(to_dimacs_int(lit));
            if (lrat_checker_enabled_)
                lrat_checker_.record_clause(event.clause_id, literals);
        }
        if ((kind == proof::event_kind::add_derived) && !antecedents.empty())
        {
            event.antecedent_ids.reserve(antecedents.size());
            for (const auto& antecedent: antecedents)
                event.antecedent_ids.push_back(antecedent);
        }
        // With buffering disabled nothing can ever read the buffer, so events are not retained; otherwise a
        // proof-free solve pays for building and later freeing one buffered event per clause action.
        if (event_buffering_enabled_)
            event_stream_.push_event(event);
        if (online_checker_enabled_)
        {
            switch (kind)
            {
                case proof::event_kind::add_original:
                case proof::event_kind::add_derived:
                    online_checker_.on_add(ref);
                    break;
                case proof::event_kind::delete_clause:
                    online_checker_.on_delete(ref);
                    break;
                case proof::event_kind::shrink_clause:
                    online_checker_.on_shrink(ref);
                    break;
                case proof::event_kind::conclusion:
                    break;
            }
        }

        if (kind == proof::event_kind::delete_clause)
        {
            if (lrat_checker_enabled_)
                lrat_checker_.forget_clause(event.clause_id);
            id_allocator_.retire_on_delete(ref);
        }

        for (auto& tracer: enabled_tracers_)
            tracer.on_event(last_event_);
        for (auto& tracer: registered_tracers_)
            tracer.on_event(last_event_);
    }
}
