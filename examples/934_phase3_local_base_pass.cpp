// 934_phase3_local_base_pass.cpp
// Tests: Phase 3 - Non-parameter base (local array)
//
// When base is a local array (not a parameter), DTT should:
// - Emit diagnostic note with local._size_bytes >= n + off
// - NOT store in FnPreconditions (no call-site checking possible)

#include <cstddef>
#include <cstring>

// Variant 1: Local array base
void inner_local(size_t off, size_t n) {
    char local[100];
    char* p = local + off;
    memcpy(p, "data", n);  // Note: local._size_bytes >= n + off
                           // No FnPrecondition (local is not a parameter)
}

// Variant 2: Local array with constant offset
void inner_local_const(size_t n) {
    char local[100];
    char* p = local + 20;
    memcpy(p, "data", n);  // Note: local._size_bytes >= n + 20
}

// Variant 3: Mix of local and param
void inner_mixed(char* param_base, size_t off, size_t n) {
    char local[50];
    char* p = local + 10;
    char* q = param_base + off;
    memcpy(p, "data", n);  // Note: local._size_bytes >= n + 10 (no FnPrecondition)
    memcpy(q, "data", n);  // Infer: param_base._size_bytes >= n + off (has FnPrecondition)
}

int main() { return 0; }
