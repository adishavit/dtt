// Inter-procedural property tracking through reference parameters
//
// WHAT IS TESTED:
// When fold push_back operates on a REFERENCE parameter (vector declared in
// caller), the _size increments happen inside the callee but DTT doesn't
// propagate them back to the caller's scope.
//
// ROOT CAUSE:
// DTT is a per-function analyzer. Method postconditions applied to reference
// parameters inside a callee are not reflected at the call site.
// This is NOT specific to fold expressions — even a non-template helper
// `add_one(v)` calling `v.push_back(1)` has the same limitation.
//
// NOTE: Local-vector fold push_back IS now tracked correctly — see
// 1210_fold_push_back_local_pass.cpp for those tests.

#include <vector>

// =============================================================================
// Gap 1: Fold push_back on reference parameter
// =============================================================================

template<typename... Args>
void add_all(std::vector<int>& v, Args... args) {
    (v.push_back(args), ...);
}

void gap1_fold_push_back_ref() {
    std::vector<int> v;
    add_all(v, 10, 20, 30);   // 3 push_backs via fold on reference param
    v[0];                      // XFAIL: caller doesn't see _size change
}

// =============================================================================
// Gap 2: Fold emplace_back on reference parameter
// =============================================================================

template<typename... Args>
void emplace_all(std::vector<int>& v, Args... args) {
    (v.emplace_back(args), ...);
}

void gap2_fold_emplace_back_ref() {
    std::vector<int> v;
    emplace_all(v, 1, 2, 3, 4, 5);
    v[0];                      // XFAIL: same issue with emplace_back
}

// =============================================================================
// Gap 3: Mixed fold with reference parameter
// =============================================================================

void sink(int);

template<typename... Args>
void push_and_sink(std::vector<int>& v, Args... args) {
    ((v.push_back(args), sink(args)), ...);
}

void gap3_nested_fold_ref() {
    std::vector<int> v;
    push_and_sink(v, 1, 2);
    v[0];                      // XFAIL: nested fold on reference param
}

int main() {
    gap1_fold_push_back_ref();
    gap2_fold_emplace_back_ref();
    gap3_nested_fold_ref();
    return 0;
}
