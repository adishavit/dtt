// 1209_pack_property_silent.cpp — DTT silent on pack property tracking
//
// These are FALSE NEGATIVES: unsafe code that DTT does not detect.
// DTT loses container _size tracking when vectors are received as
// parameter pack elements in variadic templates.
//
// When DTT is enhanced to track properties through pack parameters,
// convert to 1209_pack_property_fail.cpp.

#include <vector>
#include <utility>

// =============================================================================
// SECTION 1: OOB through fold expression on pack parameters
// =============================================================================

// Variant 1.1: fold accessing [0] on empty vectors — not caught
template<typename... Vecs>
void fold_access_first(Vecs&... vecs) {
    ((void)vecs[0], ...);  // BUG: accessing [0] on empty vectors
}

void variant1_fold_oob() {
    std::vector<int> a, b, c;  // All _size = 0
    fold_access_first(a, b, c);  // SILENT: DTT doesn't catch OOB through pack fold
}

// Variant 1.2: fold accessing [5] on vectors of size 3
template<typename... Vecs>
void fold_access_oob(Vecs&... vecs) {
    ((void)vecs[5], ...);  // BUG: 5 >= 3
}

void variant2_fold_oob_nonzero() {
    std::vector<int> a(3), b(3);  // Both _size = 3
    fold_access_oob(a, b);  // SILENT: 5 >= 3 not caught through pack
}

// =============================================================================
// SECTION 2: OOB through recursive variadic template peeling
// =============================================================================

// Base case
void recursive_access() {}

// Peel one vector, access [0], recurse on rest
template<typename First, typename... Rest>
void recursive_access(First& first, Rest&... rest) {
    (void)first[0];              // BUG: first may be empty
    recursive_access(rest...);   // Recurse
}

// Variant 2.1: all empty vectors through recursive peeling
void variant3_recursive_oob() {
    std::vector<int> v1, v2;  // Both _size = 0
    recursive_access(v1, v2);  // SILENT: OOB not caught through recursion
}

// =============================================================================
// SECTION 3: Use-after-move through fold expression
// =============================================================================

template<typename... Args>
void fold_move(Args&&... args) {
    ((void)std::move(args), ...);  // Moves each arg
}

// Variant 3.1: access after move through fold
void variant4_move_through_fold() {
    std::vector<int> v;
    v.resize(5);          // v._size = 5
    fold_move(v);         // Moves v through fold — should invalidate
    (void)v[0];           // SILENT: use-after-move not tracked through fold
}

// Variant 3.2: multiple vectors moved through fold
void variant5_multi_move_fold() {
    std::vector<int> a, b;
    a.resize(3);
    b.resize(3);
    fold_move(a, b);       // Both moved through fold
    (void)a[0];            // SILENT: use-after-move not tracked
    (void)b[0];            // SILENT: use-after-move not tracked
}

// =============================================================================
// SECTION 4: OOB through variadic wrapper function call chain
// =============================================================================

template<typename Vec>
void access_front(Vec& v) {
    (void)v[0];  // BUG: v may be empty
}

template<typename... Args>
void call_on_all(Args&... args) {
    (access_front(args), ...);  // Fold calling function on each pack element
}

// Variant 4.1: empty vector through wrapper → function chain
void variant6_wrapper_chain() {
    std::vector<int> v;  // _size = 0
    call_on_all(v);  // SILENT: _size lost through pack → function call
}

// Variant 4.2: multiple empty vectors through chain
void variant7_multi_wrapper() {
    std::vector<int> a, b;  // Both _size = 0
    call_on_all(a, b);  // SILENT: OOB not caught on either
}
