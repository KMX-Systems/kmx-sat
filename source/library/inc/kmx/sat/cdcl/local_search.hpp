/// @file inc/kmx/sat/cdcl/local_search.hpp
/// @brief Stochastic local search (probSAT and WalkSAT) used to seed branching polarities and to find models.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <algorithm>
    #include <cmath>
    #include <cstddef>
    #include <cstdint>
    #include <span>
    #include <vector>
#endif
#include <kmx/sat/cdcl/clause/database.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{
    /// @brief Stochastic local search used to seed branching polarities and, when it succeeds, to find models.
    /// @details
    /// The walk produces an assignment that satisfies as many original clauses as it can within a flip budget.
    /// The search uses it as the initial saved phase for branching, and when every clause is satisfied the CDCL
    /// search confirms the model without a single conflict. Nothing downstream trusts the result, so a poor walk
    /// costs flips and never correctness.
    ///
    /// Two flip rules are offered, because no single one is robust across formula families:
    /// - **probSAT** (Balint and Schöning): pick a falsified clause, then choose one of its variables with
    ///   probability proportional to `(eps + break)^-cb`, where `break` is the number of clauses the flip would
    ///   newly falsify. The state of the art for uniform random k-SAT; it closes 2000-variable random 3-SAT near
    ///   the threshold in about a million flips where CDCL solvers run for minutes.
    /// - **WalkSAT/SKC** (Selman, Kautz and Cohen): a zero-break variable if one exists, otherwise with the noise
    ///   probability a random variable of the clause and otherwise the least-break variable, ties broken at
    ///   random. With low noise (10-20%) it solves structured formulas -- graph colouring encodings, for example
    ///   -- on which probSAT stalls.
    ///
    /// Break counts are maintained incrementally through the critical variable of every clause with exactly one
    /// true literal, so a flip costs one pass over the flipped variable's occurrence lists and choosing a variable
    /// costs one pass over the clause. The generator is deterministic and seeded by the caller: the walk feeds
    /// branching, so repeated runs must be identical.
    /// @reference A. Balint, U. Schöning, "Choosing Probability Distributions for Stochastic Local Search and the
    /// Role of Make versus Break" (SAT 2012); B. Selman, H. Kautz, B. Cohen, "Noise Strategies for Improving
    /// Local Search" (AAAI 1994).
    class local_search final
    {
    public:
        /// @brief Flip rule of one walk.
        enum class strategy : std::uint8_t
        {
            probsat,
            walksat
        };

        /// @brief Parameters of one walk.
        struct configuration final
        {
            strategy kind {strategy::probsat};
            /// @brief WalkSAT noise probability in percent.
            std::uint32_t noise_percent {12u};
            /// @brief probSAT polynomial break exponent (2.06 is the published optimum for 3-SAT).
            double break_exponent {2.06};
            /// @brief probSAT polynomial break offset.
            double break_epsilon {0.9};
        };

        /// @brief Constructs a local search with no attached formula.
        /// @throws None (noexcept).
        local_search() noexcept = default;

        /// @brief Copies the database's original clauses into the walk's flat representation.
        /// @param database Clause database whose irredundant clauses define the formula to satisfy.
        /// @param variable_count Highest variable index in use.
        /// @return False if the formula is empty, in which case no walk can run.
        /// @throws None (noexcept).
        bool prepare(const clause::database& database, const std::uint32_t variable_count) noexcept;

        /// @brief Runs one walk over the prepared formula and records the best assignment seen.
        /// @param config Flip rule and its parameters.
        /// @param max_flips Flip budget; the walk stops early once every clause is satisfied.
        /// @param initial_phase Polarity each variable starts at (index zero unused); random where absent.
        /// @return True if an assignment satisfying every original clause was found.
        /// @throws None (noexcept).
        /// @note Never inlined, and its helpers always are: the flip loop then compiles the same way whatever the
        /// rest of the program looks like. Left to link-time optimisation, one build ran it 8% slower than another
        /// on the same flips.
        [[gnu::noinline]] bool walk(const configuration& config, const std::size_t max_flips,
                                    const std::span<const std::uint8_t> initial_phase = {}) noexcept;

        /// @brief Prepares the formula and runs a single probSAT walk; the one-call form of the walk.
        bool run(const clause::database& database, const std::uint32_t variable_count, const std::size_t max_flips,
                 const std::span<const std::uint8_t> initial_phase = {}) noexcept;

        /// @brief Returns the best assignment found, indexed by variable (index zero unused).
        /// @return Per-variable polarity of the best assignment, empty if no walk has run.
        [[nodiscard]] std::span<const std::uint8_t> best_assignment() const noexcept { return best_assignment_; }

        /// @brief Returns how many original clauses the best assignment left unsatisfied.
        [[nodiscard]] std::size_t best_unsatisfied_count() const noexcept { return best_unsatisfied_count_; }

        /// @brief Returns how many clauses the walk was prepared over.
        [[nodiscard]] std::size_t clause_count() const noexcept { return clause_offsets_.empty() ? 0u : clause_offsets_.size() - 1u; }

        /// @brief Returns the longest clause of the prepared formula.
        [[nodiscard]] std::uint32_t maximum_clause_size() const noexcept { return maximum_clause_size_; }

        /// @brief Returns the total number of flips performed since construction.
        [[nodiscard]] std::size_t flip_count() const noexcept { return flip_count_; }

        /// @brief Sets the seed of the deterministic generator driving clause and variable choice.
        void set_seed(const std::uint64_t seed) noexcept { random_state_ = seed | 1ull; }

    private:
        static constexpr std::size_t npos {static_cast<std::size_t>(-1)};
        static constexpr std::uint32_t npos32 {static_cast<std::uint32_t>(-1)};
        /// @brief Break counts at or above this share the last probability-table entry.
        static constexpr std::size_t probability_table_size {64u};

        void initialize_assignment(const std::span<const std::uint8_t> initial_phase) noexcept;

        [[nodiscard]] [[gnu::always_inline]] inline bool is_true(const std::uint32_t raw) const noexcept
        {
            return assignment_[raw >> 1u] != (raw & 1u);
        }

        /// @brief Recomputes every clause's true-literal count, critical variable, break counts and the unsatisfied list.
        void recompute_state() noexcept;

        [[gnu::noinline]] void record_best() noexcept
        {
            best_assignment_ = assignment_;
            best_unsatisfied_count_ = unsatisfied_.size();
        }

        void prepare_probabilities(const configuration& config) noexcept;

        [[nodiscard]] [[gnu::always_inline]] inline std::uint32_t select_probsat(const std::uint32_t clause_index) noexcept
        {
            const auto begin = clause_offsets_[clause_index];
            const auto end = clause_offsets_[clause_index + 1u];
            candidate_weights_.clear();
            double total {};
            for (auto position = begin; position < end; ++position)
            {
                const auto breaks = static_cast<std::size_t>(break_counts_[clause_literals_[position] >> 1u]);
                const auto weight = probabilities_[(breaks < probability_table_size) ? breaks : probability_table_size - 1u];
                candidate_weights_.push_back(weight);
                total += weight;
            }
            auto target = uniform_unit() * total;
            auto position = begin;
            for (; position + 1u < end; ++position)
            {
                const auto weight = candidate_weights_[position - begin];
                if (target < weight)
                    break;
                target -= weight;
            }
            return clause_literals_[position] >> 1u;
        }

        [[nodiscard]] [[gnu::always_inline]] inline std::uint32_t select_walksat(const std::uint32_t clause_index,
                                                                                 const configuration& config) noexcept
        {
            const auto begin = clause_offsets_[clause_index];
            const auto end = clause_offsets_[clause_index + 1u];
            std::uint32_t best_variable {};
            std::uint32_t best_breaks {npos32};
            std::uint32_t ties {};
            for (auto position = begin; position < end; ++position)
            {
                const auto candidate = clause_literals_[position] >> 1u;
                const auto breaks = break_counts_[candidate];
                if (breaks < best_breaks)
                {
                    best_breaks = breaks;
                    best_variable = candidate;
                    ties = 1u;
                }
                else if ((breaks == best_breaks) && ((next_random() % ++ties) == 0u))
                    best_variable = candidate;
            }
            // A zero-break flip is free progress and is always taken; otherwise the noise probability decides
            // between the greedy choice and a random one, which is what lets the walk escape local minima.
            if ((best_breaks != 0u) && ((next_random() % 100u) < config.noise_percent))
                return clause_literals_[begin + next_random() % (end - begin)] >> 1u;
            return best_variable;
        }

        [[gnu::always_inline]] inline void flip_variable(const std::uint32_t variable_index) noexcept
        {
            const auto was_true = assignment_[variable_index];
            assignment_[variable_index] = static_cast<std::uint8_t>(was_true ^ 1u);
            const auto losing = (variable_index << 1u) | ((was_true != 0u) ? 0u : 1u);
            const auto gaining = losing ^ 1u;

            for (auto position = occurrence_starts_[losing]; position < occurrence_starts_[losing + 1u]; ++position)
            {
                const auto clause_index = occurrences_[position];
                const auto remaining = --true_counts_[clause_index];
                if (remaining == 0u)
                {
                    unsatisfied_position_[clause_index] = static_cast<std::uint32_t>(unsatisfied_.size());
                    unsatisfied_.push_back(clause_index);
                    --break_counts_[variable_index];
                }
                else if (remaining == 1u)
                {
                    for (auto index = clause_offsets_[clause_index]; index < clause_offsets_[clause_index + 1u]; ++index)
                    {
                        const auto raw = clause_literals_[index];
                        if (is_true(raw))
                        {
                            critical_variables_[clause_index] = raw >> 1u;
                            break;
                        }
                    }
                    ++break_counts_[critical_variables_[clause_index]];
                }
            }
            for (auto position = occurrence_starts_[gaining]; position < occurrence_starts_[gaining + 1u]; ++position)
            {
                const auto clause_index = occurrences_[position];
                const auto previous = true_counts_[clause_index]++;
                if (previous == 0u)
                {
                    const auto slot = unsatisfied_position_[clause_index];
                    const auto moved = unsatisfied_.back();
                    unsatisfied_[slot] = moved;
                    unsatisfied_position_[moved] = slot;
                    unsatisfied_.pop_back();
                    unsatisfied_position_[clause_index] = npos32;
                    critical_variables_[clause_index] = variable_index;
                    ++break_counts_[variable_index];
                }
                else if (previous == 1u)
                    --break_counts_[critical_variables_[clause_index]];
            }
        }

        [[nodiscard]] [[gnu::always_inline]] inline std::uint64_t next_random() noexcept
        {
            // splitmix64: cheap, full-period, and every output bit is usable.
            std::uint64_t value = (random_state_ += 0x9e3779b97f4a7c15ull);
            value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
            value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
            return value ^ (value >> 31u);
        }

        [[nodiscard]] double uniform_unit() noexcept { return static_cast<double>(next_random() >> 11u) * (1.0 / 9007199254740992.0); }

        std::uint32_t variable_count_ {};
        std::uint32_t maximum_clause_size_ {};
        std::uint64_t random_state_ {0x9e3779b97f4a7c15ull};
        std::size_t flip_count_ {};
        std::vector<std::uint32_t> clause_literals_ {};
        std::vector<std::uint32_t> clause_offsets_ {};
        std::vector<std::uint32_t> occurrence_starts_ {};
        std::vector<std::uint32_t> occurrence_cursor_ {};
        std::vector<std::uint32_t> occurrences_ {};
        std::vector<std::uint32_t> true_counts_ {};
        std::vector<std::uint32_t> critical_variables_ {};
        std::vector<std::uint32_t> break_counts_ {};
        std::vector<std::uint32_t> unsatisfied_ {};
        std::vector<std::uint32_t> unsatisfied_position_ {};
        std::vector<std::uint8_t> assignment_ {};
        std::vector<std::uint8_t> best_assignment_ {};
        std::vector<double> probabilities_ {};
        std::vector<double> candidate_weights_ {};
        std::size_t best_unsatisfied_count_ {npos};
    };
}
