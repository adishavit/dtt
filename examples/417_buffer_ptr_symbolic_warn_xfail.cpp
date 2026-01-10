// Example 417: Buffer pointer symbolic bounds warning — XFAIL
// ============================================================================
// GAP: Symbolic runtime bounds from new[] not checked at access
// ============================================================================
//
// EXPECTED BEHAVIOR (not yet implemented):
// - p[0] should WARN because n > 0 not proved (Category D)
// - Z3 should be invoked to check if 0 < n can be proved
//
// CURRENT BEHAVIOR:
// - Compiles without error or warning
//
// This test will "fail" in ctest because WILL_FAIL expects compilation to fail,
// but it currently succeeds. This correctly indicates the gap.

#include <cstddef>

size_t get_size();

void test_symbolic_warning() {
    size_t n = get_size();
    int* p = new int[n];   // _base_length = n (symbolic)

    p[0] = 0;              // GAP: Should WARN (n > 0 not proved), but doesn't

    delete[] p;
}

int main() {
    test_symbolic_warning();
    return 0;
}
