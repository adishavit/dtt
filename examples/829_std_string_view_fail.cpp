// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 829_std_string_view_fail.cpp
// Tests: Unsafe std::string_view usage that DTT SHOULD catch

#include <string>
#include <string_view>

// =============================================================================
// SECTION 1: Access on empty string_view
// =============================================================================

// Variant 1.1: front() on default-constructed
void test_front_empty() {
    std::string_view sv;
    sv.front();  // ERROR: _size == 0  // dtt-error {{_size}}
}

// Variant 1.2: back() on default-constructed
void test_back_empty() {
    std::string_view sv;
    sv.back();  // ERROR: _size == 0  // dtt-error {{_size}}
}

// Variant 1.3: operator[] on empty
void test_subscript_empty() {
    std::string_view sv;
    sv[0];  // ERROR: _size == 0  // dtt-error {{_size}}
}

// Variant 1.4: front() on empty literal
void test_front_empty_literal() {
    std::string_view sv = "";
    sv.front();  // KNOWN GAP (not asserted): empty string
}

// =============================================================================
// SECTION 2: Out-of-bounds index access
// =============================================================================

// Variant 2.1: index == size
void test_index_equals_size() {
    std::string_view sv = "hello";
    sv[5];  // ERROR: index 5 >= size 5  // dtt-error {{_size}}
}

// Variant 2.2: index > size
void test_index_greater_size() {
    std::string_view sv = "hi";
    sv[100];  // ERROR: index 100 >= size 2  // dtt-error {{_size}}
}

// Variant 2.3: unguarded parameter index
void test_param_index(size_t index) {
    std::string_view sv = "hello";
    sv[index];  // ERROR: can't prove index < 5  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 3: Access after remove_prefix/remove_suffix
// =============================================================================

// Variant 3.1: remove_prefix makes empty
void test_remove_prefix_empty() {
    std::string_view sv = "hi";
    sv.remove_prefix(2);  // Now empty
    sv.front();  // ERROR: _size == 0  // dtt-error {{_size}}
}

// Variant 3.2: remove_suffix makes empty
void test_remove_suffix_empty() {
    std::string_view sv = "hi";
    sv.remove_suffix(2);  // Now empty
    sv.back();  // ERROR: _size == 0  // dtt-error {{_size}}
}

// Variant 3.3: excessive remove_prefix
void test_remove_prefix_overflow() {
    std::string_view sv = "hi";
    sv.remove_prefix(10);  // UB: removes more than size
    sv.front();  // ERROR  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 4: Access in wrong branch
// =============================================================================

// Variant 4.1: access in else of !empty()
void test_wrong_branch() {
    std::string_view sv;
    if (!sv.empty()) {
        // OK here
    } else {
        sv.front();  // ERROR: empty in else branch  // dtt-error {{_size}}
    }
}

// Variant 4.2: access after negated size check
void test_negated_size() {
    std::string_view sv = "hello";
    size_t index = 10;
    if (index >= sv.size()) {
        sv[index];  // ERROR: index is out of bounds here  // dtt-error {{_size}}
    }
}

// =============================================================================
// SECTION 5: Access on parameter string_view
// =============================================================================

// Variant 5.1: front() on parameter
void test_param_front(std::string_view sv) {
    sv.front();  // UNTRACKED (not asserted): unannotated receiver parameter; can't prove _size > 0
}

// Variant 5.2: operator[] on parameter
void test_param_subscript(std::string_view sv) {
    sv[0];  // ERROR: can't prove _size > 0  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 6: find() result used without guard
// =============================================================================

// Variant 6.1: use find() result without checking npos
void test_find_no_guard() {
    std::string_view sv = "hello";
    auto pos = sv.find('x');  // Returns npos
    sv[pos];  // ERROR: pos might be npos (huge value)  // dtt-error {{_size}}
}

// Variant 6.2: use find() on parameter
void test_find_param(std::string_view sv, char c) {
    auto pos = sv.find(c);
    sv[pos];  // ERROR: pos might be npos  // dtt-error {{_size}}
}

// Variant 6.3: find() != npos guard (gap: DTT should infer pos < size())
void test_find_npos_guard() {
    std::string_view sv = "hello world";
    auto pos = sv.find('w');
    if (pos != std::string_view::npos) {
        sv[pos];  // SHOULD BE OK: find() != npos implies pos < size()  // dtt-error {{_size}}
    }
}

// =============================================================================
// SECTION 7: substr with bad bounds
// =============================================================================

// Variant 7.1: substr offset > size
void test_substr_bad_offset() {
    std::string_view sv = "hello";
    auto sub = sv.substr(10);  // Throws or UB
    (void)sub;
}

// Variant 7.2: substr count too large (clamped, but still)
void test_substr_access() {
    std::string_view sv = "hello";
    auto sub = sv.substr(0, 100);  // Clamped to size
    sub[99];  // ERROR: sub.size() is only 5  // dtt-error {{_size}}
}

// =============================================================================
// SECTION 8: data() used without size check
// =============================================================================

// Variant 8.1: data() might be null on empty
void test_data_null() {
    std::string_view sv;
    const char* p = sv.data();
    *p;  // KNOWN GAP (not asserted): p might be null or empty
}

// Variant 8.2: data() accessed beyond size
void test_data_overflow() {
    std::string_view sv = "hi";
    const char* p = sv.data();
    p[10];  // KNOWN GAP (not asserted): 10 >= size
}

// =============================================================================
// SECTION 9: Dangling string_view
// =============================================================================

// Variant 9.1: string_view to temporary (dangling)
std::string_view test_dangling() {
    std::string s = "hello";
    return std::string_view(s);  // KNOWN GAP (not asserted): s destroyed, sv dangles
}

// Variant 9.2: string_view to local (returned)
std::string_view test_dangling_local() {
    char buf[10] = "test";
    return std::string_view(buf);  // KNOWN GAP (not asserted): buf destroyed
}

// =============================================================================
// SECTION 10: Wrong guard variable
// =============================================================================

// Variant 10.1: guard checks wrong string_view
void test_wrong_sv_guard(size_t index) {
    std::string_view small = "hi";
    std::string_view large = "hello world";
    if (index < large.size()) {
        small[index];  // ERROR: guarded against wrong string_view  // dtt-error {{_size}}
    }
}

int main() {
    return 0;
}
