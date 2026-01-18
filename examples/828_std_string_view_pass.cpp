// 828_std_string_view_pass.cpp
// Tests: Safe std::string_view usage that should compile without DTT errors

#include <string_view>
#include <string>

// =============================================================================
// SECTION 1: Guard with empty() before access
// =============================================================================

// Variant 1.1: if (!empty()) guard then front()
void test_empty_guard_front() {
    std::string_view sv = "hello";
    if (!sv.empty()) {
        sv.front();  // OK: guarded
    }
}

// Variant 1.2: if (!empty()) guard then back()
void test_empty_guard_back() {
    std::string_view sv = "hello";
    if (!sv.empty()) {
        sv.back();  // OK: guarded
    }
}

// Variant 1.3: early return on empty
void test_early_return() {
    std::string_view sv;
    if (sv.empty()) return;
    sv.front();  // OK: guarded
}

// =============================================================================
// SECTION 2: Guard with size() before index access
// =============================================================================

// Variant 2.1: if (index < size()) guard
void test_size_guard(size_t index) {
    std::string_view sv = "hello world";
    if (index < sv.size()) {
        sv[index];  // OK: guarded
    }
}

// Variant 2.2: if (size() > 0) guard
void test_size_positive() {
    std::string_view sv = "hello";
    if (sv.size() > 0) {
        sv[0];  // OK: guarded
    }
}

// Variant 2.3: for loop with i < size()
void test_for_loop() {
    std::string_view sv = "hello";
    for (size_t i = 0; i < sv.size(); ++i) {
        sv[i];  // OK: bounded
    }
}

// =============================================================================
// SECTION 3: Access on string literal (known size)
// =============================================================================

// Variant 3.1: direct initialization from literal
void test_literal_init() {
    std::string_view sv = "hello";
    sv.front();  // OK: "hello" has 5 chars
    sv.back();   // OK
    sv[0];       // OK
    sv[4];       // OK: last char
}

// Variant 3.2: constexpr string_view
void test_constexpr_sv() {
    constexpr std::string_view sv = "test";
    sv[0];  // OK: compile-time known size
    sv[3];  // OK
}

// =============================================================================
// SECTION 4: Range-based for
// =============================================================================

// Variant 4.1: range-for iteration
void test_range_for() {
    std::string_view sv = "hello";
    for (char c : sv) {
        (void)c;  // OK: range-for is safe
    }
}

// =============================================================================
// SECTION 5: Substring with bounds
// =============================================================================

// Variant 5.1: substr with valid bounds
void test_substr_valid() {
    std::string_view sv = "hello world";
    auto sub = sv.substr(0, 5);  // "hello"
    sub.front();  // OK: substr has 5 chars
}

// Variant 5.2: remove_prefix with guard
void test_remove_prefix() {
    std::string_view sv = "hello";
    if (sv.size() >= 2) {
        sv.remove_prefix(2);
        sv.front();  // OK: at least 3 chars remain
    }
}

// Variant 5.3: remove_suffix with guard
void test_remove_suffix() {
    std::string_view sv = "hello";
    if (sv.size() >= 2) {
        sv.remove_suffix(2);
        sv.back();  // OK: at least 3 chars remain
    }
}

// =============================================================================
// SECTION 6: at() (throws on out-of-bounds)
// =============================================================================

// Variant 6.1: at() with try-catch
void test_at_safe() {
    std::string_view sv = "hello";
    try {
        sv.at(0);  // OK: at() throws on out-of-bounds
    } catch (...) {}
}

// =============================================================================
// SECTION 7: data() with guard
// =============================================================================

// Variant 7.1: data() with null check
void test_data_guard() {
    std::string_view sv = "hello";
    const char* p = sv.data();
    if (p && !sv.empty()) {
        p[0];  // OK: guarded
    }
}

// =============================================================================
// SECTION 8: find() with guard
// =============================================================================

// Variant 8.1: find() != npos guard
void test_find_guard() {
    std::string_view sv = "hello world";
    auto pos = sv.find('w');
    if (pos != std::string_view::npos) {
        sv[pos];  // OK: guarded
    }
}

// Variant 8.2: find() result used in substr
void test_find_substr() {
    std::string_view sv = "hello world";
    auto pos = sv.find(' ');
    if (pos != std::string_view::npos && pos < sv.size()) {
        sv.substr(0, pos);  // OK: guarded
    }
}

// =============================================================================
// SECTION 9: Comparison-based access
// =============================================================================

// Variant 9.1: starts_with implies size (C++20)
void test_starts_with() {
    std::string_view sv = "hello world";
    if (sv.starts_with("hello")) {
        sv[4];  // OK: at least 5 chars if starts_with "hello"
    }
}

// Variant 9.2: length() guard (same as size())
void test_length_guard() {
    std::string_view sv = "hello";
    if (sv.length() >= 3) {
        sv[2];  // OK: guarded
    }
}

// =============================================================================
// SECTION 10: Copy from std::string
// =============================================================================

// Variant 10.1: string_view from non-empty string
void test_from_string() {
    std::string s = "hello";
    std::string_view sv = s;
    if (!sv.empty()) {
        sv.front();  // OK: guarded
    }
}

// =============================================================================
// SECTION 11: Iterator-based access
// =============================================================================

// Variant 11.1: begin/end iteration (uses range-for which is safer)
void test_iterator() {
    std::string_view sv = "hello";
    for (char c : sv) {
        (void)c;  // OK: range-for handles bounds
    }
}

// =============================================================================
// SECTION 12: compare() doesn't affect size
// =============================================================================

// Variant 12.1: compare doesn't change size
void test_compare() {
    std::string_view sv = "hello";
    sv.compare("world");
    sv.front();  // OK: compare is const
}

int main() {
    return 0;
}
