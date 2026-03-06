// 1802_taint_guarded_pass.cpp
// Tests: Internal (non-tainted) values should not trigger taint warnings
// Only taint-specific diagnostics should fire on tainted data;
// internal data should produce regular DTT diagnostics (not taint ones)

#include <cstdlib>
#include <cstring>

// Internal function — no taint source annotation
size_t compute_length();
char* get_internal_data();

// Variant 1: Internal function calls are not taint sources
void test_internal_not_tainted() {
    char* data = get_internal_data();  // Not tainted (no taint source)
    system(data);                      // OK from taint perspective
    // (may trigger other DTT diagnostics, but not taint)
}

// Variant 2: Constants are not tainted
void test_constant_not_tainted() {
    system("ls -la");                  // OK: constant string, not tainted
}

// Variant 3: Local computation not tainted
void test_local_computation() {
    char buf[32] = "echo hello";
    system(buf);                       // OK: locally initialized, not tainted
}

int main() { return 0; }
