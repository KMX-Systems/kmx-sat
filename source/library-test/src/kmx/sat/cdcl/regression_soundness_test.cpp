/// @file library-test/src/kmx/sat/cdcl/regression_soundness_test.cpp
/// @brief Regression tests for the soundness defects found while tuning the engine against the classic SATLIB set.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <kmx/sat/cdcl/solver_core.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/proof/event_stream.hpp>
#include <kmx/sat/proof/tracer/drat.hpp>
#include <kmx/sat/proof/tracer/view.hpp>
#include <kmx/sat/solve_request.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    namespace
    {
        using clause_t = std::vector<literal>;

        literal dimacs(const int value) noexcept
        {
            return literal {variable {static_cast<variable::index_t>(value < 0 ? -value : value)}, value < 0};
        }

        clause_t clause_of(const std::initializer_list<int> values)
        {
            clause_t clause;
            for (const auto value: values)
                clause.push_back(dimacs(value));
            return clause;
        }

        void add(solver_core& solver, const clause_t& clause) { solver.add_problem_clause(std::span<const literal> {clause}); }

        /// @brief True if every clause has a literal the model makes true; a variable the model omits counts as false.
        bool satisfies(const std::span<const literal> model, const std::vector<clause_t>& clauses, const std::uint32_t variables)
        {
            std::vector<std::int8_t> value(static_cast<std::size_t>(variables) + 1u, std::int8_t {-1});
            for (const auto lit: model)
                if (lit.variable_of().index() <= variables)
                    value[lit.variable_of().index()] = lit.is_negated() ? std::int8_t {-1} : std::int8_t {1};
            for (const auto& clause: clauses)
            {
                bool satisfied = false;
                for (const auto lit: clause)
                    satisfied |= (value[lit.variable_of().index()] > 0) != lit.is_negated();
                if (!satisfied)
                    return false;
            }
            return true;
        }

        /// @brief Exhaustive satisfiability check for formulas of a dozen variables or fewer.
        bool brute_force_satisfiable(const std::vector<clause_t>& clauses, const std::uint32_t variables)
        {
            for (std::uint32_t assignment = 0u; assignment < (1u << variables); ++assignment)
            {
                bool all = true;
                for (const auto& clause: clauses)
                {
                    bool satisfied = false;
                    for (const auto lit: clause)
                    {
                        const bool is_true = ((assignment >> (lit.variable_of().index() - 1u)) & 1u) != 0u;
                        satisfied |= is_true != lit.is_negated();
                    }
                    if (!satisfied)
                    {
                        all = false;
                        break;
                    }
                }
                if (all)
                    return true;
            }
            return false;
        }

        /// @brief Deterministic generator so a failure reproduces from its seed.
        struct generator final
        {
            std::uint64_t state;
            std::uint32_t next(const std::uint32_t bound) noexcept
            {
                state = state * 6364136223846793005ull + 1442695040888963407ull;
                return static_cast<std::uint32_t>((state >> 33u) % bound);
            }
        };
    }

    TEST_CASE("lucky assignments respect the root unit clauses", "[sat][regression]")
    {
        // A unit clause lives outside the watch lists. The first lucky check assigned every variable false without
        // assigning the units first, satisfied every watched clause, and reported that assignment as a model.
        solver_core solver;
        add(solver, clause_of({1}));
        add(solver, clause_of({2, 3}));
        add(solver, clause_of({-2, 4}));
        add(solver, clause_of({-3, -4, 5}));
        const std::vector<clause_t> clauses {clause_of({1}), clause_of({2, 3}), clause_of({-2, 4}), clause_of({-3, -4, 5})};
        REQUIRE(solver.solve({}) == solver_core::status::satisfiable);
        const auto model = solver.extract_internal_model();
        REQUIRE(satisfies(model, clauses, 5u));
        const auto first = std::find_if(model.begin(), model.end(), [](const literal lit) { return lit.variable_of().index() == 1u; });
        REQUIRE(first != model.end());
        REQUIRE_FALSE(first->is_negated());
    }

    TEST_CASE("a root conflict found before the search is reported as unsatisfiable", "[sat][regression]")
    {
        // The propagator counts the trail literal that exposed a conflict as propagated, so a conflict found by
        // root propagation before the search (in the lucky check or in probing) was not rediscovered by the
        // search, which then continued as if the formula were consistent.
        SECTION("refuted by propagating the input units")
        {
            solver_core solver;
            add(solver, clause_of({1}));
            add(solver, clause_of({2}));
            add(solver, clause_of({-1, -2, 3}));
            add(solver, clause_of({-3, 4}));
            add(solver, clause_of({-3, -4}));
            REQUIRE(solver.solve({}) == solver_core::status::unsatisfiable);
        }
        SECTION("refuted by failed-literal probing")
        {
            solver_core solver;
            add(solver, clause_of({1, 2}));
            add(solver, clause_of({1, -2}));
            add(solver, clause_of({-1, 3}));
            add(solver, clause_of({-1, -3}));
            REQUIRE(solver.solve({}) == solver_core::status::unsatisfiable);
        }
        SECTION("the satisfiable twin is not affected")
        {
            solver_core solver;
            add(solver, clause_of({1, 2}));
            add(solver, clause_of({1, -2}));
            add(solver, clause_of({-1, 3}));
            add(solver, clause_of({-1, -3, 4}));
            REQUIRE(solver.solve({}) == solver_core::status::satisfiable);
        }
    }

    TEST_CASE("units derived by probing honour assumptions", "[sat][regression]")
    {
        // Probing derives ~1 at the root from (~1 v 3) and (~1 v ~3); an assumption of 1 must then be reported as
        // the failed core, not silently overridden, and the opposite assumption must still be satisfiable.
        const std::vector<clause_t> clauses {clause_of({-1, 3}), clause_of({-1, -3}), clause_of({2, 4}), clause_of({-2, 5})};
        {
            solver_core solver;
            for (const auto& clause: clauses)
                add(solver, clause);
            solve_request request {};
            request.assumptions = {dimacs(1)};
            REQUIRE(solver.solve(request) == solver_core::status::unsatisfiable);
            const auto core = solver.extract_failed_core();
            REQUIRE(std::find(core.begin(), core.end(), dimacs(1)) != core.end());
        }
        {
            solver_core solver;
            for (const auto& clause: clauses)
                add(solver, clause);
            solve_request request {};
            request.assumptions = {dimacs(-1)};
            REQUIRE(solver.solve(request) == solver_core::status::satisfiable);
            REQUIRE(satisfies(solver.extract_internal_model(), clauses, 5u));
        }
    }

    TEST_CASE("factoring keeps the reported model within the original variables", "[sat][regression]")
    {
        // A 3x3 rectangle (a_i v b_j) is what bounded variable addition rewrites with a fresh variable; the model
        // handed back must mention only the original variables and satisfy the original clauses.
        std::vector<clause_t> clauses;
        for (int left = 1; left <= 3; ++left)
            for (int right = 4; right <= 6; ++right)
                clauses.push_back(clause_of({left, right}));
        clauses.push_back(clause_of({-4, -5, -6})); // some b_j is false, so every a_i must be true
        clauses.push_back(clause_of({-1, 7}));
        clauses.push_back(clause_of({-2, -7, 8}));
        solver_core solver;
        for (const auto& clause: clauses)
            add(solver, clause);
        REQUIRE(solver.solve({}) == solver_core::status::satisfiable);
        const auto model = solver.extract_internal_model();
        for (const auto lit: model)
            REQUIRE(lit.variable_of().index() <= 8u);
        REQUIRE(satisfies(model, clauses, 8u));
    }

    TEST_CASE("a fresh factoring variable never captures a clause-free problem variable", "[sat][regression]")
    {
        // Variable 7 is declared through an assumption only; the rectangle over 1..6 must not be factored with a
        // fresh variable numbered 7, or the assumption would constrain the definition instead of the problem.
        std::vector<clause_t> clauses;
        for (int left = 1; left <= 3; ++left)
            for (int right = 4; right <= 6; ++right)
                clauses.push_back(clause_of({left, -right}));
        for (const bool negative: {false, true})
        {
            solver_core solver;
            for (const auto& clause: clauses)
                add(solver, clause);
            solve_request request {};
            request.assumptions = {literal {variable {7u}, negative}};
            REQUIRE(solver.solve(request) == solver_core::status::satisfiable);
            const auto model = solver.extract_internal_model();
            REQUIRE(satisfies(model, clauses, 7u));
            const auto seventh = std::find_if(model.begin(), model.end(), [](const literal lit) { return lit.variable_of().index() == 7u; });
            REQUIRE(seventh != model.end());
            REQUIRE(seventh->is_negated() == negative);
        }
    }

    TEST_CASE("small random formulas are decided correctly", "[sat][regression]")
    {
        // Every pre-search step (probing, lifting, the lucky check) and every mode of the search must agree with
        // exhaustive enumeration on formulas small enough to enumerate. The stale lifting mark that answered
        // three satisfiable IBM instances unsatisfiable is the kind of defect this catches.
        generator random {0x9e3779b97f4a7c15ull};
        std::size_t satisfiable_count {};
        for (std::size_t sample = 0u; sample < 400u; ++sample)
        {
            const std::uint32_t variables = 5u + random.next(6u); // 5..10
            const std::uint32_t clause_count = 2u * variables + random.next(2u * variables + 1u);
            std::vector<clause_t> clauses;
            for (std::uint32_t index = 0u; index < clause_count; ++index)
            {
                const std::uint32_t size = 1u + random.next(3u); // 1..3, binary-heavy enough for probing to run
                clause_t clause;
                while (clause.size() < size)
                {
                    const auto var = 1u + random.next(variables);
                    const bool seen = std::any_of(clause.begin(), clause.end(),
                                                  [var](const literal lit) { return lit.variable_of().index() == var; });
                    if (!seen)
                        clause.push_back(literal {variable {var}, random.next(2u) != 0u});
                }
                clauses.push_back(clause);
            }
            solver_core solver;
            for (const auto& clause: clauses)
                add(solver, clause);
            const auto verdict = solver.solve({});
            const bool expected = brute_force_satisfiable(clauses, variables);
            INFO("sample " << sample << " variables " << variables << " clauses " << clause_count);
            if (expected)
            {
                REQUIRE(verdict == solver_core::status::satisfiable);
                REQUIRE(satisfies(solver.extract_internal_model(), clauses, variables));
                ++satisfiable_count;
            }
            else
                REQUIRE(verdict == solver_core::status::unsatisfiable);
        }
        // The generator must produce both kinds, or the test proves little.
        REQUIRE(satisfiable_count > 20u);
        REQUIRE(satisfiable_count < 380u);
    }

    TEST_CASE("the first phase decides a small formula without preprocessing it", "[sat][regression]")
    {
        // x1 xor x2 xor x3 = 0 and = 1 together: no lucky assignment satisfies it and root propagation derives
        // nothing, so only the first phase's search can decide it, within a budget of two propagations per
        // literal. The pipeline must never run for it.
        solver_core solver;
        for (const auto& clause: {clause_of({1, 2, 3}), clause_of({1, -2, -3}), clause_of({-1, 2, -3}), clause_of({-1, -2, 3}),
                                  clause_of({-1, -2, -3}), clause_of({-1, 2, 3}), clause_of({1, -2, 3}), clause_of({1, 2, -3})})
            add(solver, clause);
        REQUIRE(solver.solve({}) == solver_core::status::unsatisfiable);
        REQUIRE(solver.preprocess_run_count() == 0u);
        REQUIRE(solver.conflict_event_count() >= 1u);
    }

    TEST_CASE("the walk stays off when probing derived a unit", "[sat][regression]")
    {
        // Random 3-SAT at the threshold is what the walk is for: on this unsatisfiable instance the search
        // outlives the opening probe and the walk runs. Two binary clauses on fresh variables make failed-literal
        // probing derive a unit, and that structure turns the walk off for the episode (`hanoi4`: 718k flips
        // stuck at two unsatisfied clauses for a search that finished 230 conflicts later).
        generator random {0x9e377fb97f4a7627ull};
        std::vector<clause_t> clauses;
        for (std::size_t index = 0u; index < 645u; ++index)
        {
            clause_t clause;
            while (clause.size() < 3u)
            {
                const auto var = 1u + random.next(150u);
                if (std::none_of(clause.begin(), clause.end(), [var](const literal lit) { return lit.variable_of().index() == var; }))
                    clause.push_back(literal {variable {var}, random.next(2u) != 0u});
            }
            clauses.push_back(clause);
        }
        {
            solver_core plain;
            for (const auto& clause: clauses)
                add(plain, clause);
            REQUIRE(plain.solve({}) == solver_core::status::unsatisfiable);
            REQUIRE(plain.probe_unit_count() == 0u);
            REQUIRE(plain.walk_flip_count() > 0u);
        }
        {
            solver_core structured;
            for (const auto& clause: clauses)
                add(structured, clause);
            add(structured, clause_of({151, 152}));
            add(structured, clause_of({151, -152}));
            REQUIRE(structured.solve({}) == solver_core::status::unsatisfiable);
            REQUIRE(structured.probe_unit_count() >= 1u);
            REQUIRE(structured.walk_flip_count() == 0u);
        }
    }

    TEST_CASE("a formula the first phase cannot decide is searched exactly as without it", "[sat][regression]")
    {
        // The pigeonhole formula with six holes outlives the first phase's budget. Everything the first phase
        // touched (clauses, literal order, phases, activities, counters) goes back, so the second phase must take
        // the very same decisions as a solve that skipped the first phase, which a request with an explicit
        // conflict limit does.
        const auto pigeonhole = [](solver_core& solver)
        {
            constexpr int holes = 6, pigeons = 7;
            const auto var = [](const int pigeon, const int hole) { return pigeon * holes + hole + 1; };
            for (int pigeon = 0; pigeon < pigeons; ++pigeon)
            {
                clause_t clause;
                for (int hole = 0; hole < holes; ++hole)
                    clause.push_back(dimacs(var(pigeon, hole)));
                add(solver, clause);
            }
            for (int hole = 0; hole < holes; ++hole)
                for (int first = 0; first < pigeons; ++first)
                    for (int second = first + 1; second < pigeons; ++second)
                        add(solver, clause_of({-var(first, hole), -var(second, hole)}));
        };
        solver_core with_first_phase;
        pigeonhole(with_first_phase);
        REQUIRE(with_first_phase.solve({}) == solver_core::status::unsatisfiable);
        REQUIRE(with_first_phase.raw_probe_conflict_count() >= 1u);
        REQUIRE(with_first_phase.preprocess_run_count() == 1u);

        solver_core without_first_phase;
        pigeonhole(without_first_phase);
        solve_request limited {};
        limited.conflict_limit = 1'000'000'000u;
        REQUIRE(without_first_phase.solve(limited) == solver_core::status::unsatisfiable);
        REQUIRE(without_first_phase.raw_probe_conflict_count() == 0u);

        REQUIRE(with_first_phase.decision_event_count() == without_first_phase.decision_event_count());
        REQUIRE(with_first_phase.conflict_event_count() == without_first_phase.conflict_event_count());
    }

    TEST_CASE("a proof tracer attached after clauses were added names every clause", "[sat][regression]")
    {
        // Without a consumer no proof event is dispatched and no clause id exists; a tracer attached later must
        // adopt every live clause, so deletions and antecedents of clauses that predate it carry valid ids.
        solver_core solver;
        add(solver, clause_of({1, 2}));
        add(solver, clause_of({-1, 2}));
        add(solver, clause_of({1, -2}));
        REQUIRE(solver.proof_buffered_event_count() == 0u);

        proof::tracer::view sink {proof::tracer::drat {}};
        solver.attach_proof_tracer(sink);
        add(solver, clause_of({-1, -2}));
        REQUIRE(solver.solve({}) == solver_core::status::unsatisfiable);

        const auto events = solver.buffered_proof_events();
        REQUIRE_FALSE(events.empty());
        for (const auto& event: events)
        {
            if (event.kind == proof::event_kind::conclusion)
                continue;
            INFO("event kind " << static_cast<int>(event.kind));
            REQUIRE(event.clause_id.valid());
            for (const auto antecedent: event.antecedent_ids)
                REQUIRE(antecedent.valid());
        }
    }
}
