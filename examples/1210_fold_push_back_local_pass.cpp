// 1210_fold_push_back_local_pass.cpp
// Tests: Fold expression push_back counting on LOCAL vectors — should compile clean
//
// When a fold expression like `(v.push_back(args), ...)` operates on a local
// vector, DTT analyzes the template instantiation where the fold is expanded
// into N individual push_back calls, correctly tracking _size.

#include <vector>

// =============================================================================
// SECTION 1: Basic fold push_back on local vector
// =============================================================================

// Variant 1.1: comma fold push_back then access
template<typename... Args>
void fold_push_back_then_access(Args... args) {
    std::vector<int> v;
    (v.push_back(args), ...);  // N push_backs via fold
    v[0];                      // OK: _size = N after fold expansion
}

void variant1_three_pushbacks() {
    fold_push_back_then_access(1, 2, 3);  // 3 push_backs -> _size = 3
}

// Variant 1.2: single element fold
void variant2_single_pushback() {
    fold_push_back_then_access(42);  // 1 push_back -> _size = 1
}

// Variant 1.3: many elements
void variant3_many_pushbacks() {
    fold_push_back_then_access(1, 2, 3, 4, 5, 6, 7, 8);  // 8 push_backs
}

// =============================================================================
// SECTION 2: Fold emplace_back on local vector
// =============================================================================

template<typename... Args>
void fold_emplace_back_then_access(Args... args) {
    std::vector<int> v;
    (v.emplace_back(args), ...);
    v[0];                      // OK: _size = N
}

void variant4_emplace_back() {
    fold_emplace_back_then_access(10, 20, 30);
}

// =============================================================================
// SECTION 3: Fold push_back with mixed operations
// =============================================================================

void sink(int);

template<typename... Args>
void fold_push_and_use(Args... args) {
    std::vector<int> v;
    ((v.push_back(args), sink(args)), ...);  // Nested comma in fold
    v[0];                                     // OK: _size = N
}

void variant5_push_and_sink() {
    fold_push_and_use(1, 2);
}

// =============================================================================
// SECTION 4: Fold push_back followed by size check
// =============================================================================

template<typename... Args>
int fold_push_and_front(Args... args) {
    std::vector<int> v;
    (v.push_back(args), ...);
    return v.front();          // OK: !empty() after N push_backs
}

void variant6_front_after_fold() {
    int x = fold_push_and_front(1, 2, 3);
    (void)x;
}

// =============================================================================
// SECTION 5: Multiple fold operations on same local vector
// =============================================================================

template<typename... Args>
void fold_double_push(Args... args) {
    std::vector<int> v;
    (v.push_back(args), ...);  // N push_backs
    (v.push_back(args), ...);  // N more push_backs -> _size = 2N
    v[0];                       // OK: _size = 2N
}

void variant7_double_fold() {
    fold_double_push(1, 2, 3);  // _size = 6 after both folds
}

int main() {
    variant1_three_pushbacks();
    variant2_single_pushback();
    variant3_many_pushbacks();
    variant4_emplace_back();
    variant5_push_and_sink();
    variant6_front_after_fold();
    variant7_double_fold();
    return 0;
}
