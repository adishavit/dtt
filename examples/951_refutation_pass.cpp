// 951_refutation_pass.cpp
// Tests: Counterexample generation for buffer bounds checking
//
// When DTT cannot verify buffer bounds, it emits a note with concrete values.
// This helps users understand WHY verification failed.
//
// Expected diagnostics:
//   warning: cannot verify memset size fits in N-byte buffer
//   note: counterexample: buf._size_bytes = N, required = <expr>
//   note: 'ptr' is untracked (no size information available)

#include <cstring>
#include <cstdlib>

// Test 1: Symbolic bounds with concrete buffer
// Expected: warning + note showing counterexample
void test_symbolic_bounds(size_t n) {
    char buf[4];          // buf._size_bytes = 4
    memset(buf, 0, n);    // Warning with counterexample: buf._size_bytes = 4, required = n
}

// Test 2: Untracked pointer
// Expected: note explaining pointer has no tracking
void test_untracked(char* p, size_t n) {
    memcpy(p, "data", n);  // Note: 'p' is untracked (no size information available)
}

// Test 3: Both source and dest tracking
void test_both_buffers(size_t n) {
    char src[8];
    char dst[4];
    memcpy(dst, src, n);  // Counterexample for dst bounds
}

int main() {
    test_symbolic_bounds(100);
    char buf[10];
    test_untracked(buf, 100);
    test_both_buffers(10);
    return 0;
}
