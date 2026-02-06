// XFAIL: Fold expression push_back counting — should COMPILE but currently FAILS
//
// WHAT IS TESTED:
// After a fold expression like `(v.push_back(args), ...)` with N pack elements,
// DTT should know that v._size increased by N. Direct push_back calls are tracked
// correctly, but the fold-expanded equivalent is not.
//
// WHY IT'S DIFFICULT:
// 1. Fold expressions `(expr, ...)` expand the pack at AST level, but DTT sees
//    the unexpanded pattern, not the N individual push_back calls
// 2. To count fold push_backs, DTT would need to know sizeof...(args) at the
//    instantiation site and multiply the push_back effect by N
// 3. Each fold iteration may have different side effects — DTT would need to
//    distinguish "uniform" folds (same method on same object) from complex ones
//
// CURRENT BEHAVIOR:
// DTT counts direct push_back calls (v.push_back(1); v.push_back(2); → _size=2)
// but does NOT count push_backs inside fold expressions.
// After (v.push_back(args), ...) with 3 args, DTT still thinks _size = 0.
// This causes false positives: v[0] is incorrectly rejected.

#include <vector>

// =============================================================================
// Gap 1: Comma fold push_back — count lost
// =============================================================================

// After 3 fold-expanded push_backs, v[0] should be safe
template<typename... Args>
void fold_push_back_then_access(Args... args) {
    std::vector<int> v;
    (v.push_back(args), ...);  // N push_backs via fold
    v[0];                      // XFAIL: DTT thinks _size = 0, but it's actually N
}

void gap1_fold_push_back() {
    fold_push_back_then_access(1, 2, 3);  // 3 push_backs → _size = 3
}

// =============================================================================
// Gap 2: Fold push_back on reference parameter
// =============================================================================

template<typename... Args>
void add_all(std::vector<int>& v, Args... args) {
    (v.push_back(args), ...);
}

void gap2_fold_push_back_ref() {
    std::vector<int> v;
    add_all(v, 10, 20, 30);   // 3 push_backs via fold
    v[0];                      // XFAIL: DTT lost the _size increment
}

// =============================================================================
// Gap 3: Fold emplace_back — same issue
// =============================================================================

template<typename... Args>
void emplace_all(std::vector<int>& v, Args... args) {
    (v.emplace_back(args), ...);
}

void gap3_fold_emplace_back() {
    std::vector<int> v;
    emplace_all(v, 1, 2, 3, 4, 5);   // 5 emplace_backs
    v[0];                              // XFAIL: DTT doesn't count fold emplace_backs
}

// =============================================================================
// Gap 4: Mixed fold — push_back in fold with other operations
// =============================================================================

void sink(int);

template<typename... Args>
void push_and_sink(std::vector<int>& v, Args... args) {
    ((v.push_back(args), sink(args)), ...);  // Nested comma in fold
}

void gap4_nested_fold() {
    std::vector<int> v;
    push_and_sink(v, 1, 2);
    v[0];  // XFAIL: DTT can't track push_back inside nested fold comma expressions
}

int main() {
    gap1_fold_push_back();
    gap2_fold_push_back_ref();
    gap3_fold_emplace_back();
    gap4_nested_fold();
    return 0;
}
