// Example 415: Buffer pointer _size in requires annotation — XFAIL
// ============================================================================
// GAP: _size/_size_bytes not yet supported in requires annotations for pointers
// ============================================================================
//
// EXPECTED BEHAVIOR (not yet implemented):
// - process(arr, 15) should FAIL because arr._size=10 < 15
// - The requires annotation should check buf._size >= n at call site
//
// CURRENT BEHAVIOR:
// - Compiles without error (annotation ignored for pointer parameters)
//
// This test will "fail" in ctest because WILL_FAIL expects compilation to fail,
// but it currently succeeds. This correctly indicates the gap.

#include <cstddef>

[[clang::annotate("dtt:requires=buf._size >= n")]]
void process(int* buf, size_t n) {
    buf[n - 1] = 0;  // Should be OK: n-1 < n <= buf._size
}

void test_requires_annotation() {
    int arr[10];
    process(arr, 5);   // Should pass: arr._size=10 >= 5
    process(arr, 15);  // GAP: Should FAIL (arr._size=10 < 15), but doesn't
}

int main() {
    test_requires_annotation();
    return 0;
}
