// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 801_std_vector_xfail.cpp
// Tests: Unsafe std::vector usage that DTT SHOULD catch but doesn't yet
// These are xfail (expected fail) - will be renamed to _fail.cpp when implemented

#include <vector>
#include <cstddef>

// =============================================================================
// SECTION 1: Access on empty/unknown size
// =============================================================================

// Variant 1.1: front() on default-constructed vector
void test_front_empty() {
    std::vector<int> v;
    v.front();  // ERROR: _size unknown (default constructed, no push)  // dtt-error {{_size}}
}

// Variant 1.2: back() on empty vector
void test_back_empty() {
    std::vector<int> v;
    v.back();  // ERROR: _size == 0  // dtt-error {{_size}}
}

// Variant 1.3: operator[] on empty vector
void test_index_empty() {
    std::vector<int> v;
    v[0];  // ERROR: _size == 0  // dtt-error {{_size}}
}

// Variant 1.4: pop_back() on empty vector
void test_pop_empty() {
    std::vector<int> v;
    v.pop_back();  // ERROR: requires _size > 0  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 2: Out-of-bounds index
// =============================================================================

// Variant 2.1: Index beyond tracked size
void test_index_oob() {
    std::vector<int> v;
    v.push_back(1);  // _size = 1
    v[5];            // ERROR: 5 >= _size  // dtt-error {{_size}}
}

// Variant 2.2: Index at exact size (off-by-one)
void test_index_at_size() {
    std::vector<int> v;
    v.push_back(1);
    v.push_back(2);  // _size = 2
    v[2];            // ERROR: 2 >= _size (off-by-one)  // dtt-error {{_size}}
}

// Variant 2.3: Index with wrong guard
void test_index_wrong_guard() {
    std::vector<int> v;
    v.push_back(1);
    if (v.size() >= 1) {
        v[1];  // ERROR: size() >= 1 only proves v[0] is safe  // dtt-error {{_size}}
    }
}

// =============================================================================
// SECTION 3: Access after clear()
// =============================================================================

// Variant 3.1: front() after clear()
void test_front_after_clear() {
    std::vector<int> v;
    v.push_back(1);
    v.clear();   // _size = 0
    v.front();   // ERROR: _size == 0  // dtt-error {{_size}}
}

// Variant 3.2: operator[] after clear()
void test_index_after_clear() {
    std::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.clear();
    v[0];  // ERROR: _size == 0  // dtt-error {{_size}}
}

// Variant 3.3: back() after clear()
void test_back_after_clear() {
    std::vector<int> v;
    v.push_back(1);
    v.clear();
    v.back();  // ERROR: _size == 0  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 4: Access after pop_back() empties
// =============================================================================

// Variant 4.1: operator[] after pop_back empties
void test_index_after_pop() {
    std::vector<int> v;
    v.push_back(1);  // _size = 1
    v.pop_back();    // _size = 0
    v[0];            // ERROR: _size == 0  // dtt-error {{_size}}
}

// Variant 4.2: front() after multiple pops empty vector
void test_front_after_pops() {
    std::vector<int> v;
    v.push_back(1);
    v.push_back(2);  // _size = 2
    v.pop_back();    // _size = 1
    v.pop_back();    // _size = 0
    v.front();       // ERROR: _size == 0  // dtt-error {{_size}}
}

// Variant 4.3: back() after pop to empty
void test_back_after_pop() {
    std::vector<int> v;
    v.push_back(1);
    v.pop_back();
    v.back();  // ERROR: _size == 0  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 5: resize() to zero then access
// =============================================================================

// Variant 5.1: resize(0) then access
void test_resize_zero() {
    std::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.resize(0);  // _size = 0
    v[0];         // ERROR: _size == 0  // dtt-error {{_size}}
}

// Variant 5.2: resize smaller then access old index
void test_resize_smaller() {
    std::vector<int> v;
    v.push_back(1);
    v.push_back(2);
    v.push_back(3);  // _size = 3
    v.resize(1);     // _size = 1
    v[2];            // ERROR: 2 >= _size  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 6: erase() all then access
// =============================================================================

// Variant 6.1: erase() all elements
void test_erase_all() {
    std::vector<int> v;
    v.push_back(1);
    v.erase(v.begin());  // _size = 0
    v.front();           // ERROR: _size == 0  // dtt-error {{_size}}
}

// Variant 6.2: erase() range to empty
void test_erase_range() {
    std::vector<int> v = {1, 2, 3};
    v.erase(v.begin(), v.end());  // _size = 0
    v[0];  // ERROR: _size == 0  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 7: Unguarded access to parameter vector
// =============================================================================

// Variant 7.1: Unguarded front() on parameter
void test_param_front(std::vector<int>& v) {
    v.front();  // UNTRACKED (not asserted): unannotated receiver parameter; can't prove _size > 0
}

// Variant 7.2: Unguarded back() on parameter
void test_param_back(std::vector<int>& v) {
    v.back();  // UNTRACKED (not asserted): unannotated receiver parameter; can't prove _size > 0
}

// Variant 7.3: Unguarded index on parameter
void test_param_index(std::vector<int>& v) {
    v[0];  // ERROR: can't prove _size > 0  // dtt-error {{_size}}
}

// Variant 7.4: Unguarded index with parameter
void test_param_both(std::vector<int>& v, size_t idx) {
    v[idx];  // ERROR: can't prove idx < _size  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 8: Wrong branch access
// =============================================================================

// Variant 8.1: Access in else branch of empty() check
void test_wrong_branch() {
    std::vector<int> v;
    if (v.empty()) {
        v.front();  // ERROR: empty() is true here!  // dtt-error {{_size}}
    }
}

// Variant 8.2: Access outside guard scope
void test_outside_guard() {
    std::vector<int> v;
    v.push_back(1);
    if (!v.empty()) {
        // OK here
    }
    v.pop_back();
    v.front();  // ERROR: guard doesn't protect this  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 9: reserve() doesn't enable access
// =============================================================================

// Variant 9.1: reserve() then access
void test_reserve_access() {
    std::vector<int> v;
    v.reserve(100);  // capacity = 100, but _size = 0
    v[0];            // ERROR: _size == 0, reserve doesn't change size  // dtt-error {{_size}}
}

// Variant 9.2: reserve() then front()
void test_reserve_front() {
    std::vector<int> v;
    v.reserve(10);
    v.front();  // ERROR: _size == 0  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 10: assign() then access beyond
// =============================================================================

// Variant 10.1: assign() then access beyond
void test_assign_oob() {
    std::vector<int> v;
    v.assign(3, 42);  // _size = 3
    v[5];             // ERROR: 5 >= _size  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 11: data() pointer without guard
// =============================================================================

// Variant 11.1: data() access without guard
void test_data_unguarded() {
    std::vector<int> v;
    int* p = v.data();
    p[0];  // KNOWN GAP (not asserted): _size == 0
}

// Variant 11.2: data() access beyond size
void test_data_oob() {
    std::vector<int> v;
    v.push_back(1);  // _size = 1
    int* p = v.data();
    p[5];  // KNOWN GAP (not asserted): 5 >= _size
}

// =============================================================================
// SECTION 12: Comparison with wrong value
// =============================================================================

// Variant 12.1: size() > 1 but access [1]
void test_size_gt_one() {
    std::vector<int> v;
    v.push_back(1);  // _size = 1
    if (v.size() > 1) {
        // Won't execute, but...
    }
    v[1];  // ERROR: can't prove 1 < _size  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 13: swap() loses tracking
// =============================================================================

// Variant 13.1: swap with unknown vector
void test_swap_unknown(std::vector<int>& other) {
    std::vector<int> v;
    v.push_back(1);  // _size = 1
    v.swap(other);   // Now v has unknown _size from other
    v[0];            // ERROR: can't prove _size > 0 after swap with unknown  // dtt-error {{_size}}
}

int main() {
    // These all should fail
    return 0;
}
