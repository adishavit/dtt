// 832b_variant_gaps_pass.cpp
// Feature verification tests documenting which variant features work.
// Gaps are documented in separate xfail/silent test files:
// - 832d_variant_ref_invalidation_silent.cpp (reference invalidation gap)
// - 832g_variant_switch_xfail.cpp (switch case proof extraction gap)

#include <variant>
#include <string>

// =============================================================================
// GAP TEST 1: v.index() guards
// Plan says: "v.index() guards - Proof extraction from index checks"
// =============================================================================

void test_index_guard_if() {
    std::variant<int, std::string> v = 42;
    if (v.index() == 0) {
        std::get<0>(v);  // Should be OK - index guard
    }
}

void test_index_guard_switch() {
    std::variant<int, double, std::string> v = 3.14;
    switch (v.index()) {
        case 1:
            std::get<1>(v);  // Should be OK - switch case guard
            break;
        default:
            break;
    }
}

// =============================================================================
// GAP TEST 2: emplace tracking
// Plan says: "emplace tracking - Update index on v.emplace<I>()"
// =============================================================================

// RESULT: GAP - emplace tracking NOT working (loses tracking)
// void test_emplace_by_index() {
//     std::variant<int, std::string> v;
//     v.emplace<1>("hello");
//     std::get<1>(v);  // Should be OK - just emplaced at index 1
// }

// RESULT: GAP - emplace tracking NOT working (loses tracking)
// void test_emplace_by_type() {
//     std::variant<int, std::string> v;
//     v.emplace<std::string>("hello");
//     std::get<std::string>(v);  // Should be OK - just emplaced string
// }

// =============================================================================
// GAP TEST 3: Reference invalidation
// Plan says: "Reference invalidation - Detect dangling refs after reassignment"
// =============================================================================

// RESULT: GAP - reference invalidation NOT detected (passes without error)
void test_reference_invalidation() {
    std::variant<int, std::string> v = 42;
    int& ref = std::get<int>(v);
    (void)ref;  // Use ref
    v = std::string("hello");  // Reassignment should invalidate ref
    // ref is now dangling - DTT does NOT catch this
    // ref = 10;  // BUG: using dangling reference - GAP
}

// =============================================================================
// GAP TEST 4: Move invalidation
// Plan says: "Copy/move semantics - Track index through copies and moves"
// =============================================================================

// RESULT: WORKS - DTT catches moved-from access (marks as "index unknown")
void test_move_invalidation() {
    std::variant<int, std::string> v1 = std::string("hello");
    std::variant<int, std::string> v2 = std::move(v1);
    (void)v2;
    // v1 is moved-from - DTT does catch this
    // std::get<std::string>(v1);  // WORKS: DTT reports "solver variant index unknown"
}

// =============================================================================
// GAP TEST 5: Swap tracking
// Plan says in "WILL NOT": "swap semantics - Treat as unknown after swap"
// But xfail tests suggest it should work
// =============================================================================

// RESULT: WORKS (unexpected!) - Plan said "WILL NOT" but DTT actually tracks swap correctly!
void test_swap_tracking() {
    std::variant<int, std::string> v1 = 42;
    std::variant<int, std::string> v2 = std::string("hello");
    std::swap(v1, v2);
    // After swap: v1 holds string, v2 holds int
    // DTT correctly tracks this!
    // std::get<int>(v1);  // WORKS: DTT catches "requires index 0, but v1._variant_index = 1"
}

// =============================================================================
// GAP TEST 6: Negated guards (else branch)
// =============================================================================

void test_else_branch_guard() {
    std::variant<int, std::string> v = 42;
    if (std::holds_alternative<std::string>(v)) {
        // v holds string here
    } else {
        // v does NOT hold string - but does it hold int?
        // For variant<int, string>, !holds<string> implies holds<int>
        std::get<int>(v);  // Should be OK in else branch
    }
}

// =============================================================================
// GAP TEST 7: Monostate default
// =============================================================================

void test_monostate_default() {
    std::variant<std::monostate, int, std::string> v;  // default is monostate (index 0)
    if (v.index() == 0) {
        std::get<std::monostate>(v);  // Should be OK
    }
}

int main() {
    test_index_guard_if();
    test_index_guard_switch();
    // test_emplace_by_index();   // GAP
    // test_emplace_by_type();    // GAP
    test_reference_invalidation();
    test_move_invalidation();
    test_swap_tracking();
    test_else_branch_guard();
    test_monostate_default();
    return 0;
}
