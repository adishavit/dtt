// 803_std_string_fail.cpp
// Tests: Unsafe std::string usage that DTT SHOULD catch but doesn't yet

#include <string>
#include <cstddef>

// =============================================================================
// SECTION 1: Access on empty/unknown size
// =============================================================================

// Variant 1.1: front() on empty string
void test_front_empty() {
    std::string s;
    s.front();  // ERROR: _size == 0
}

// Variant 1.2: back() on empty string
void test_back_empty() {
    std::string s;
    s.back();  // ERROR: _size == 0
}

// Variant 1.3: operator[] on empty string
void test_index_empty() {
    std::string s;
    s[0];  // ERROR: _size == 0
}

// Variant 1.4: pop_back() on empty string
void test_pop_empty() {
    std::string s;
    s.pop_back();  // ERROR: requires _size > 0
}

// =============================================================================
// SECTION 2: Out-of-bounds index
// =============================================================================

// Variant 2.1: Index beyond string length
void test_index_oob() {
    std::string s = "hi";  // _size = 2
    s[5];                  // ERROR: 5 >= _size
}

// Variant 2.2: Index at exact size (off-by-one)
void test_index_at_size() {
    std::string s = "hello";  // _size = 5
    s[5];                     // ERROR: 5 >= _size
}

// Variant 2.3: Index with wrong guard
void test_index_wrong_guard() {
    std::string s = "hi";
    if (s.size() >= 1) {
        s[2];  // ERROR: size() >= 1 only proves s[0] safe
    }
}

// =============================================================================
// SECTION 3: Access after clear()
// =============================================================================

// Variant 3.1: front() after clear()
void test_front_after_clear() {
    std::string s = "hello";
    s.clear();   // _size = 0
    s.front();   // ERROR: _size == 0
}

// Variant 3.2: operator[] after clear()
void test_index_after_clear() {
    std::string s = "hello";
    s.clear();
    s[0];  // ERROR: _size == 0
}

// Variant 3.3: back() after clear()
void test_back_after_clear() {
    std::string s = "hello";
    s.clear();
    s.back();  // ERROR: _size == 0
}

// =============================================================================
// SECTION 4: Access after pop_back() empties
// =============================================================================

// Variant 4.1: operator[] after pop_back empties
void test_index_after_pop() {
    std::string s = "x";  // _size = 1
    s.pop_back();         // _size = 0
    s[0];                 // ERROR: _size == 0
}

// Variant 4.2: front() after pops empty string
void test_front_after_pops() {
    std::string s = "ab";  // _size = 2
    s.pop_back();          // _size = 1
    s.pop_back();          // _size = 0
    s.front();             // ERROR: _size == 0
}

// =============================================================================
// SECTION 5: resize() to zero then access
// =============================================================================

// Variant 5.1: resize(0) then access
void test_resize_zero() {
    std::string s = "hello";
    s.resize(0);  // _size = 0
    s[0];         // ERROR: _size == 0
}

// Variant 5.2: resize smaller then access old index
void test_resize_smaller() {
    std::string s = "hello";  // _size = 5
    s.resize(2);              // _size = 2
    s[4];                     // ERROR: 4 >= _size
}

// =============================================================================
// SECTION 6: erase() all then access
// =============================================================================

// Variant 6.1: erase() all chars
void test_erase_all() {
    std::string s = "hello";
    s.erase();    // _size = 0
    s.front();    // ERROR: _size == 0
}

// Variant 6.2: erase() to empty via range
void test_erase_range() {
    std::string s = "hello";
    s.erase(0, s.size());  // _size = 0
    s[0];  // ERROR: _size == 0
}

// =============================================================================
// SECTION 7: Unguarded access to parameter string
// =============================================================================

// Variant 7.1: Unguarded front() on parameter
void test_param_front(std::string& s) {
    s.front();  // ERROR: can't prove _size > 0
}

// Variant 7.2: Unguarded back() on parameter
void test_param_back(std::string& s) {
    s.back();  // ERROR: can't prove _size > 0
}

// Variant 7.3: Unguarded index on parameter
void test_param_index(std::string& s) {
    s[0];  // ERROR: can't prove _size > 0
}

// Variant 7.4: Unguarded index with parameter
void test_param_both(std::string& s, size_t idx) {
    s[idx];  // ERROR: can't prove idx < _size
}

// =============================================================================
// SECTION 8: Wrong branch access
// =============================================================================

// Variant 8.1: Access in wrong branch
void test_wrong_branch() {
    std::string s;
    if (s.empty()) {
        s.front();  // ERROR: empty() is true here
    }
}

// Variant 8.2: Access outside guard scope
void test_outside_guard() {
    std::string s = "x";
    if (!s.empty()) {
        // OK here
    }
    s.pop_back();
    s.front();  // ERROR: guard doesn't protect this
}

// =============================================================================
// SECTION 9: reserve() doesn't enable access
// =============================================================================

// Variant 9.1: reserve() then access
void test_reserve_access() {
    std::string s;
    s.reserve(100);  // capacity = 100, but _size = 0
    s[0];            // ERROR: _size == 0
}

// Variant 9.2: reserve() then front()
void test_reserve_front() {
    std::string s;
    s.reserve(10);
    s.front();  // ERROR: _size == 0
}

// =============================================================================
// SECTION 10: c_str()/data() without guard
// =============================================================================

// Variant 10.1: c_str() access without guard
void test_c_str_unguarded() {
    std::string s;
    const char* p = s.c_str();
    (void)p[0];  // ERROR: _size == 0 (but p[0] is '\0', technically OK?)
    (void)p[1];  // ERROR: definitely OOB
}

// Variant 10.2: data() access beyond size
void test_data_oob() {
    std::string s = "hi";  // _size = 2
    const char* p = s.data();
    (void)p[5];  // ERROR: 5 >= _size
}

// =============================================================================
// SECTION 11: swap() loses tracking
// =============================================================================

// Variant 11.1: swap with unknown string
void test_swap_unknown(std::string& other) {
    std::string s = "hello";
    s.swap(other);  // Now s has unknown _size from other
    s[0];           // ERROR: can't prove _size > 0
}

// =============================================================================
// SECTION 12: assign() then access beyond
// =============================================================================

// Variant 12.1: assign() then access beyond
void test_assign_oob() {
    std::string s;
    s.assign("hi");  // _size = 2
    s[5];            // ERROR: 5 >= _size
}

int main() {
    return 0;
}
