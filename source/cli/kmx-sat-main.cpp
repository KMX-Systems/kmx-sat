/// @file cli/kmx-sat-main.cpp
/// @brief Command-line SAT solver wrapper for DIMACS CNF input.
/// @details Reads standard DIMACS format, invokes solver, outputs DIMACS standard status.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include <kmx/sat/literal.hpp>
#include <kmx/sat/solve_request.hpp>
#include <kmx/sat/solver.hpp>
#include <kmx/sat/variable.hpp>

namespace fs = std::filesystem;

/// @brief Retained copy of the parsed formula, used to verify a reported model independently of the solver.
/// @details Literals are stored flat in DIMACS encoding with a `0` terminator after each clause, which keeps the
/// retained copy to one `std::int32_t` per literal instead of a vector per clause.
struct cnf_input final
{
    std::vector<std::int32_t> literals {};
    std::int32_t declared_variable_count {};
};

static int read_dimacs_cnf(const fs::path& cnf_file, kmx::sat::solver& solver, cnf_input* const retained) noexcept
{
    FILE* file = std::fopen(cnf_file.c_str(), "rb");
    if (!file)
    {
        std::cerr << "Error: cannot open file " << cnf_file << "\n";
        return 1;
    }

    char buffer[65536];
    std::size_t buf_pos = 0;
    std::size_t buf_len = 0;

    const auto read_char = [&]() noexcept -> int
    {
        if (buf_pos >= buf_len)
        {
            buf_len = std::fread(buffer, 1, sizeof(buffer), file);
            buf_pos = 0;
            if (buf_len == 0)
                return EOF;
        }
        return static_cast<unsigned char>(buffer[buf_pos++]);
    };

    int ch = 0;
    while ((ch = read_char()) != EOF)
    {
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
            continue;
        if (ch == 'c')
        {
            while ((ch = read_char()) != EOF && ch != '\n')
                ;
            continue;
        }
        // SATLIB CNF files terminate the clause list with a '%' line followed by a stray '0'. Without this
        // stop, that '0' closes an empty clause, which silently makes any such formula unsatisfiable.
        if (ch == '%')
            break;
        if (ch == 'p')
        {
            while ((ch = read_char()) != EOF && ch != '\n')
            {
                if (ch == 'c' && (ch = read_char()) == 'n' && (ch = read_char()) == 'f')
                {
                    while ((ch = read_char()) != EOF && (ch == ' ' || ch == '\t'))
                        ;
                    int vars = 0;
                    while (ch >= '0' && ch <= '9')
                    {
                        vars = vars * 10 + (ch - '0');
                        ch = read_char();
                    }
                    if (vars > 0)
                    {
                        solver.reserve(static_cast<kmx::sat::variable::index_t>(vars));
                        if (retained != nullptr)
                            retained->declared_variable_count = vars;
                    }
                }
            }
            continue;
        }

        int sign = 1;
        if (ch == '-')
        {
            sign = -1;
            ch = read_char();
        }
        else if (ch == '+')
        {
            ch = read_char();
        }

        if (ch >= '0' && ch <= '9')
        {
            int val = 0;
            while (ch >= '0' && ch <= '9')
            {
                val = val * 10 + (ch - '0');
                ch = read_char();
            }
            if (val == 0)
            {
                solver.add_literal(kmx::sat::literal(0));
                if (retained != nullptr)
                    retained->literals.push_back(0);
            }
            else
            {
                kmx::sat::variable var(static_cast<kmx::sat::variable::index_t>(val));
                solver.add_literal(kmx::sat::literal {var, sign < 0});
                if (retained != nullptr)
                    retained->literals.push_back(sign < 0 ? -val : val);
            }
        }
    }

    std::fclose(file);
    return 0;
}

/// @brief Expands a reported model into a dense, 1-based truth-value table.
/// @details Entry `index` is `1` when variable `index` is true and `0` when it is false. Variables declared in the
/// header but absent from the model occur in no clause, so they are defaulted to true to keep the emitted `v` line a
/// total assignment over the declared range, as the DIMACS output convention expects.
/// @param model Model literals reported by the solver.
/// @param declared_variable_count Variable count from the `p cnf` header, or zero when it was absent.
/// @return Truth-value table indexed by variable, with a unused slot at index zero.
/// @throws std::bad_alloc If the table cannot be allocated.
static std::vector<std::int8_t> model_values(const std::span<const kmx::sat::literal> model,
                                             const std::int32_t declared_variable_count) noexcept(false)
{
    const auto declared_bound = declared_variable_count > 0 ? static_cast<std::size_t>(declared_variable_count) : 0u;
    const auto bound = model.size() > declared_bound ? model.size() : declared_bound;

    std::vector<std::int8_t> values(bound + 1u, std::int8_t {1});
    for (const auto lit: model)
    {
        const auto index = static_cast<std::size_t>(lit.variable_of().index());
        if (index != 0u && index < values.size())
            values[index] = lit.is_negated() ? std::int8_t {0} : std::int8_t {1};
    }

    return values;
}

/// @brief Checks the reported model against the retained formula, independently of the solver's own state.
/// @param cnf Retained copy of the parsed formula.
/// @param values Truth-value table produced by `model_values`.
/// @return One-based index of the first unsatisfied clause, or zero when every clause is satisfied.
/// @throws None (noexcept).
static std::size_t first_unsatisfied_clause(const cnf_input& cnf, const std::vector<std::int8_t>& values) noexcept
{
    std::size_t clause_index = 1u;
    bool satisfied = false;

    for (const auto dimacs_literal: cnf.literals)
    {
        if (dimacs_literal == 0)
        {
            if (!satisfied)
                return clause_index;
            ++clause_index;
            satisfied = false;
            continue;
        }

        if (satisfied)
            continue;

        const auto index = static_cast<std::size_t>(dimacs_literal < 0 ? -dimacs_literal : dimacs_literal);
        if (index < values.size() && values[index] == (dimacs_literal > 0 ? std::int8_t {1} : std::int8_t {0}))
            satisfied = true;
    }

    return 0u;
}

/// @brief Writes the model as DIMACS `v` lines, wrapped near the conventional line-width limit.
/// @param values Truth-value table produced by `model_values`.
/// @throws std::bad_alloc If a line buffer cannot be allocated.
static void print_model(const std::vector<std::int8_t>& values) noexcept(false)
{
    static constexpr std::size_t max_line_width = 78u;

    std::string line {"v"};
    for (std::size_t index = 1u; index < values.size(); ++index)
    {
        const auto signed_index = static_cast<std::int64_t>(index);
        const auto token = std::to_string(values[index] == 0 ? -signed_index : signed_index);
        if (line.size() + token.size() + 1u > max_line_width)
        {
            std::cout << line << '\n';
            line = "v";
        }
        line += ' ';
        line += token;
    }

    if (line.size() + 2u > max_line_width)
    {
        std::cout << line << '\n';
        line = "v";
    }

    line += " 0";
    std::cout << line << '\n';
}

/// @brief Writes the command-line usage summary.
/// @param program Invoked program name, as given in `argv[0]`.
/// @throws None (noexcept).
static void print_usage(const char* const program) noexcept
{
    std::cout << "usage: " << program << " <cnf-file> [options]\n\n"
              << "Reads DIMACS CNF and reports the SAT competition status line, plus a `v` model\n"
              << "line when the formula is satisfiable. Exit code 10 = SATISFIABLE,\n"
              << "20 = UNSATISFIABLE, 0 = UNKNOWN, 1 = usage or input error.\n\n"
              << "Options:\n"
              << "  --assume <lit>                     Assume a literal for this episode; repeatable.\n"
              << "  --decision-limit <n>               Stop after n decisions (0 = unlimited).\n"
              << "  --conflict-limit <n>               Stop after n conflicts (0 = unlimited).\n"
              << "  --restart-interval <n>             Conflicts between scheduled restarts.\n"
              << "  --decision-restart-interval <n>    Decisions between scheduled restarts.\n"
              << "  --reduction-interval <n>           Conflicts between clause-database reductions.\n"
              << "  --reduction-fraction-percent <n>   Percentage of ranked learned clauses to drop.\n"
              << "  --no-model                         Suppress the `v` line on a satisfiable result.\n"
              << "  --no-verify                        Skip the independent model check before printing.\n"
              << "  -h, --help                         Show this message.\n";
}

int main(int argc, char* argv[])
{
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);

    for (int index = 1; index < argc; ++index)
    {
        const std::string option = argv[index];
        if (option == "--help" || option == "-h")
        {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (argc < 2)
    {
        print_usage(argv[0]);
        return 1;
    }

    const fs::path cnf_file = argv[1];
    if (!fs::is_regular_file(cnf_file))
    {
        std::cerr << "Error: not a regular file: " << cnf_file << "\n";
        return 1;
    }

    bool emit_model = true;
    bool verify_model = true;
    for (int index = 2; index < argc; ++index)
    {
        const std::string option = argv[index];
        if (option == "--no-model")
            emit_model = false;
        else if (option == "--no-verify")
            verify_model = false;
    }

    kmx::sat::solver solver;
    cnf_input cnf {};
    const int read_status = read_dimacs_cnf(cnf_file, solver, verify_model ? &cnf : nullptr);
    if (read_status != 0)
        return read_status;

    kmx::sat::solve_request request;
    for (int index = 2; index < argc; ++index)
    {
        const std::string option = argv[index];
        if (option == "--no-model" || option == "--no-verify")
            continue;
        else if (option == "--assume" && index + 1 < argc)
        {
            const int value = std::atoi(argv[++index]);
            if (value == 0)
            {
                std::cerr << "Error: assumption literal must be nonzero\n";
                return 1;
            }
            request.assumptions.emplace_back(kmx::sat::variable(static_cast<kmx::sat::variable::index_t>(std::abs(value))), value < 0);
        }
        else if (option == "--decision-limit" && index + 1 < argc)
            request.decision_limit = std::strtoull(argv[++index], nullptr, 10);
        else if (option == "--conflict-limit" && index + 1 < argc)
            request.conflict_limit = std::strtoull(argv[++index], nullptr, 10);
        else if (option == "--restart-interval" && index + 1 < argc)
            solver.set_option("restart_interval", std::strtoull(argv[++index], nullptr, 10));
        else if (option == "--decision-restart-interval" && index + 1 < argc)
            solver.set_option("decision_restart_interval", std::strtoull(argv[++index], nullptr, 10));
        else if (option == "--reduction-interval" && index + 1 < argc)
            solver.set_option("reduction_interval", std::strtoull(argv[++index], nullptr, 10));
        else if (option == "--reduction-fraction-percent" && index + 1 < argc)
            solver.set_option("reduction_fraction_percent", std::strtoull(argv[++index], nullptr, 10));
        else
        {
            std::cerr << "Error: unknown or incomplete option: " << option << "\n";
            return 1;
        }
    }
    const auto result = solver.solve(request);
    const auto statistics = solver.statistics();
    std::cout << "c kmx-stats conflicts=" << statistics.conflicts << " decisions=" << statistics.decisions
              << " propagations=" << statistics.propagations << " restarts=" << statistics.restarts
              << " learned_clauses=" << statistics.learned_clauses << " learned_clause_glue_total=" << statistics.learned_clause_glue_total
              << " learned_clause_glue_samples=" << statistics.learned_clause_glue_samples
              << " reduction_passes=" << statistics.reduction_passes << " reduced_clauses=" << statistics.reduced_clauses
              << " deleted_clauses=" << statistics.deleted_clauses << " proof_events=" << solver.proof_buffered_event_count()
              << " proof_buffered_payload_bytes=" << solver.proof_buffered_payload_bytes() << "\n";

    switch (result.status_of())
    {
        case kmx::sat::solve_result::status::satisfiable:
        {
            const auto values = model_values(result.model().values(), cnf.declared_variable_count);
            if (verify_model)
            {
                const auto unsatisfied_clause = first_unsatisfied_clause(cnf, values);
                if (unsatisfied_clause != 0u)
                {
                    std::cerr << "Error: reported model does not satisfy clause " << unsatisfied_clause
                              << " of " << cnf_file << "; refusing to emit an invalid model\n";
                    std::cout << "s UNKNOWN\n";
                    return 1;
                }
            }
            std::cout << "s SATISFIABLE\n";
            if (emit_model)
                print_model(values);
            return 10;
        }
        case kmx::sat::solve_result::status::unsatisfiable:
            std::cout << "s UNSATISFIABLE\n";
            return 20;
        case kmx::sat::solve_result::status::unknown:
        case kmx::sat::solve_result::status::terminated:
        default:
            std::cout << "s UNKNOWN\n";
            return 0;
    }
}
