#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <kmx/sat/cdcl/bank/watch_list.hpp>
#include <kmx/sat/cdcl/watch.hpp>
#include <kmx/sat/literal.hpp>
#include <kmx/sat/variable.hpp>

namespace kmx::sat::cdcl
{

    TEST_CASE("watch list", "[sat]")
    {
        using namespace kmx::sat;
        using namespace kmx::sat::cdcl;

        bank::watch_list watches;

        const literal lit_a {variable {1}, false};
        const literal lit_b {variable {2}, false};
        const cdcl::clause::ref_t ref_1 {1};
        const cdcl::clause::ref_t ref_2 {2};
        const cdcl::clause::ref_t ref_3 {3};

        watches.watch_literal(lit_a, watch {lit_b, ref_1});
        watches.watch_literal(lit_a, watch {lit_b, ref_2, true});
        REQUIRE(watches.size_of(lit_a) == 2);
        REQUIRE(watches.size_of(lit_b) == 0);

        std::vector<cdcl::clause::ref_t> visited {};
        watches.iterate(lit_a, [&](const watch& entry) noexcept { visited.push_back(entry.clause_ref()); });
        REQUIRE(visited.size() == 2);
        REQUIRE(visited[0] == ref_1);
        REQUIRE(visited[1] == ref_2);

        // Unwatching removes only the matching entry (identity by clause_ref).
        watches.unwatch_literal(lit_a, watch {lit_b, ref_1});
        REQUIRE(watches.size_of(lit_a) == 1);

        // GC relocation rewrites the surviving entry's clause_ref in place.
        watches.replace_clause_ref_after_gc(ref_2, ref_3);
        std::vector<cdcl::clause::ref_t> after_gc {};
        watches.iterate(lit_a, [&](const watch& entry) noexcept { after_gc.push_back(entry.clause_ref()); });
        REQUIRE(after_gc.size() == 1);
        REQUIRE(after_gc[0] == ref_3);

        // Compaction remaps every populated literal partition to its new literal.
        const literal lit_c {variable {3}, false};
        watches.reindex_after_compaction([&](const literal old_lit) noexcept { return old_lit == lit_a ? lit_c : old_lit; });
        REQUIRE(watches.size_of(lit_a) == 0);
        REQUIRE(watches.size_of(lit_c) == 1);

        // Rewriting a reference that is not present leaves the watch list unchanged.
        watches.replace_clause_ref_after_gc(ref_1, ref_2);
        std::vector<cdcl::clause::ref_t> unchanged_after_noop {};
        watches.iterate(lit_c, [&](const watch& entry) noexcept { unchanged_after_noop.push_back(entry.clause_ref()); });
        REQUIRE(unchanged_after_noop.size() == 1);
        REQUIRE(unchanged_after_noop[0] == ref_3);

        // flush_large_watches prunes entries whose clause is reported garbage.
        watches.watch_literal(lit_c, watch {lit_b, ref_1});
        REQUIRE(watches.size_of(lit_c) == 2);
        watches.flush_large_watches([&](const cdcl::clause::ref_t ref) noexcept { return ref == ref_3; });
        std::vector<cdcl::clause::ref_t> after_flush {};
        watches.iterate(lit_c, [&](const watch& entry) noexcept { after_flush.push_back(entry.clause_ref()); });
        REQUIRE(after_flush.size() == 1);
        REQUIRE(after_flush[0] == ref_1);

        // removed std::cout: "watch list test passed\n";
    }

} // namespace
