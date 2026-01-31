// 951_refutation_xfail.cpp
// Tests: Refutation / counterexample generation when verification fails
//
// GAP: When DTT says "cannot verify X", it should provide a counterexample
// showing WHY the verification failed - concrete values that violate the property.
//
// Currently DTT just says "cannot verify" without explanation.
// After implementation, the error should include something like:
// "Counterexample: when n=100, buf._size_bytes=4, 4 >= 100 is false"
//
// This is an xfail because DTT should fail compilation with better diagnostics.

#include <cstring>
#include <cstdlib>

// Annotated vector for DTT
struct Vec {
    int* _data;
    int _size;
    int _capacity;
};

[[clang::annotate("dtt:requires=_size > 0")]]
int front(Vec& v) { return v._data[0]; }

// Test 1: Simple constraint violation - should show counterexample
// Expected diagnostic improvement:
//   Before: "cannot verify v._size > 0"
//   After:  "cannot verify v._size > 0. Counterexample: v._size = 0"
void test_simple_violation() {
    Vec v;
    v._size = 0;  // Concrete value available
    front(v);     // FAIL: Should show counterexample with v._size = 0
}

// Test 2: Symbolic bounds - should show counterexample path
// Expected:
//   "cannot verify buf._size_bytes >= n. Counterexample: buf._size_bytes=4, n=100"
void test_symbolic_bounds(size_t n) {
    char buf[4];          // buf._size_bytes = 4
    memset(buf, 0, n);    // FAIL: Should show counterexample when n > 4
}

// Test 3: Guard not taken - should show counterexample path
// Expected:
//   "cannot verify v._size > 0. In else-branch where v._size <= 0"
void test_guard_not_taken(Vec& v) {
    if (v._size > 0) {
        front(v);  // OK: guarded
    } else {
        front(v);  // FAIL: Should show we're in else where v._size <= 0
    }
}

// Test 4: Multiple constraints - should show which one fails
// Expected:
//   "cannot verify p._size_bytes >= n. p is untracked (no size information)"
void test_untracked(char* p, size_t n) {
    memcpy(p, "data", n);  // FAIL: Should explain p has no tracking
}

// Test 5: Expression evaluation - should show concrete evaluation
// Expected:
//   "cannot verify buf._size_bytes >= (n + offset).
//    Counterexample: buf._size_bytes=10, n=8, offset=5, n+offset=13 > 10"
void test_expression(size_t n, size_t offset) {
    char buf[10];
    memset(buf + offset, 0, n);  // FAIL: Should show concrete math
}

int main() {
    return 0;
}
