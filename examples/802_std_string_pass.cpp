// 802_std_string_pass.cpp
// Tests: Safe std::string usage patterns that should compile without DTT errors

#include <string>
#include <cstddef>

// =============================================================================
// SECTION 1: empty() guards
// =============================================================================

// Variant 1.1: !empty() guard before front()
void test_empty_guard_front() {
    std::string s = "hello";
    if (!s.empty()) {
        s.front();  // OK: !empty() proves _size > 0
    }
}

// Variant 1.2: !empty() guard before back()
void test_empty_guard_back() {
    std::string s = "hello";
    if (!s.empty()) {
        s.back();  // OK
    }
}

// Variant 1.3: !empty() guard before operator[]
void test_empty_guard_index() {
    std::string s = "hello";
    if (!s.empty()) {
        s[0];  // OK
    }
}

// =============================================================================
// SECTION 2: size()/length() guards
// =============================================================================

// Variant 2.1: size() > 0 guard
void test_size_gt_zero() {
    std::string s = "test";
    if (s.size() > 0) {
        s.front();  // OK
        s.back();   // OK
    }
}

// Variant 2.2: length() > 0 guard (same as size())
void test_length_gt_zero() {
    std::string s = "test";
    if (s.length() > 0) {
        s[0];  // OK
    }
}

// Variant 2.3: size() guard for specific index
void test_size_guard_index() {
    std::string s = "hello";
    if (s.size() > 3) {
        s[3];  // OK: size() > 3 proves _size >= 4
    }
}

// Variant 2.4: size() guard with variable
void test_size_guard_variable(size_t idx) {
    std::string s = "hello";
    if (idx < s.size()) {
        s[idx];  // OK: idx < size() proves idx < _size
    }
}

// =============================================================================
// SECTION 3: String literal initialization
// =============================================================================

// Variant 3.1: String literal assignment
void test_literal_assign() {
    std::string s = "hello";  // _size = 5
    s[0];  // OK
    s[4];  // OK
}

// Variant 3.2: String literal constructor
void test_literal_ctor() {
    std::string s("world");  // _size = 5
    s.front();  // OK
    s.back();   // OK
}

// Variant 3.3: Empty string literal
void test_empty_literal_guard() {
    std::string s = "";  // _size = 0
    if (!s.empty()) {
        s[0];  // OK: guarded (won't execute)
    }
}

// =============================================================================
// SECTION 4: append/+= tracking
// =============================================================================

// Variant 4.1: append() increases size
void test_append() {
    std::string s;
    s.append("hello");  // _size = 5
    s.front();          // OK
}

// Variant 4.2: += operator
void test_plus_equals() {
    std::string s;
    s += "test";  // _size = 4
    s[0];         // OK
}

// Variant 4.3: += char
void test_plus_equals_char() {
    std::string s;
    s += 'a';  // _size = 1
    s.front();  // OK
}

// Variant 4.4: Multiple appends
void test_multiple_appends() {
    std::string s;
    s += "hello";  // _size = 5
    s += " ";      // _size = 6
    s += "world";  // _size = 11
    s[10];         // OK: _size >= 11
}

// =============================================================================
// SECTION 5: push_back tracking
// =============================================================================

// Variant 5.1: push_back char
void test_push_back() {
    std::string s;
    s.push_back('x');  // _size = 1
    s.front();         // OK
}

// Variant 5.2: Multiple push_backs
void test_push_back_multiple() {
    std::string s;
    s.push_back('a');
    s.push_back('b');
    s.push_back('c');  // _size = 3
    s[0];  // OK
    s[1];  // OK
    s[2];  // OK
}

// =============================================================================
// SECTION 6: clear() and rebuild
// =============================================================================

// Variant 6.1: clear() then append
void test_clear_append() {
    std::string s = "hello";
    s.clear();       // _size = 0
    s.append("hi");  // _size = 2
    s.front();       // OK
}

// Variant 6.2: clear() then guard
void test_clear_guard() {
    std::string s = "hello";
    s.clear();
    if (!s.empty()) {
        s[0];  // OK: guarded (won't execute)
    }
}

// =============================================================================
// SECTION 7: resize() tracking
// =============================================================================

// Variant 7.1: resize() to known size
void test_resize() {
    std::string s;
    s.resize(10);  // _size = 10
    s[0];   // OK
    s[9];   // OK
}

// Variant 7.2: resize() then guard
void test_resize_guard(size_t n) {
    std::string s;
    s.resize(n);  // _size = n
    if (n > 0) {
        s[0];  // OK
    }
}

// =============================================================================
// SECTION 8: assign() tracking
// =============================================================================

// Variant 8.1: assign() from literal
void test_assign_literal() {
    std::string s;
    s.assign("hello");  // _size = 5
    s[0];  // OK
    s[4];  // OK
}

// Variant 8.2: assign() with count
void test_assign_count() {
    std::string s;
    s.assign(5, 'x');  // _size = 5
    s[0];  // OK
    s[4];  // OK
}

// =============================================================================
// SECTION 9: for loop patterns
// =============================================================================

// Variant 9.1: Classic index-based for loop
void test_for_index() {
    std::string s = "hello";
    for (size_t i = 0; i < s.size(); ++i) {
        s[i];  // OK: i < size()
    }
}

// Variant 9.2: Range-based for
void test_for_range() {
    std::string s = "hello";
    for (char c : s) {
        (void)c;  // OK: range-based for is safe
    }
}

// =============================================================================
// SECTION 10: substr() (creates new string)
// =============================================================================

// Variant 10.1: substr() result
void test_substr() {
    std::string s = "hello world";
    std::string sub = s.substr(0, 5);  // sub._size = 5
    if (!sub.empty()) {
        sub[0];  // OK: guarded
    }
}

// =============================================================================
// SECTION 11: copy constructor / assignment
// =============================================================================

// Variant 11.1: copy constructor
void test_copy_ctor() {
    std::string s1 = "hello";
    std::string s2(s1);  // s2._size = s1._size
    if (!s2.empty()) {
        s2[0];  // OK
    }
}

// Variant 11.2: copy assignment
void test_copy_assign() {
    std::string s1 = "hello";
    std::string s2;
    s2 = s1;
    if (!s2.empty()) {
        s2[0];  // OK
    }
}

// =============================================================================
// SECTION 12: move constructor / assignment
// =============================================================================

// Variant 12.1: move constructor
void test_move_ctor() {
    std::string s1 = "hello";
    std::string s2(std::move(s1));
    if (!s2.empty()) {
        s2[0];  // OK
    }
}

// =============================================================================
// SECTION 13: insert() tracking
// =============================================================================

// Variant 13.1: insert() single char
void test_insert_char() {
    std::string s = "hllo";
    s.insert(1, 1, 'e');  // _size = 5
    if (s.size() >= 5) {
        s[4];  // OK
    }
}

// =============================================================================
// SECTION 14: erase() then guard
// =============================================================================

// Variant 14.1: erase() then guard
void test_erase_guard() {
    std::string s = "hello";
    s.erase(0, 3);  // s = "lo", _size = 2
    if (s.size() >= 2) {
        s[1];  // OK
    }
}

// =============================================================================
// SECTION 15: swap() tracking
// =============================================================================

// Variant 15.1: swap with known string
void test_swap() {
    std::string s1 = "hi";
    std::string s2 = "hello";
    s1.swap(s2);
    if (s1.size() >= 5) {
        s1[4];  // OK
    }
}

// =============================================================================
// SECTION 16: at() is bounds-checked
// =============================================================================

// Variant 16.1: at() is always safe (throws)
void test_at() {
    std::string s = "hello";
    s.at(0);  // OK: at() throws on OOB
}

// =============================================================================
// SECTION 17: c_str()/data() with guard
// =============================================================================

// Variant 17.1: c_str() with size guard
void test_c_str() {
    std::string s = "hello";
    if (s.size() >= 5) {
        const char* p = s.c_str();
        (void)p[4];  // OK
    }
}

// Variant 17.2: data() with guard
void test_data() {
    std::string s = "hello";
    if (s.size() >= 5) {
        const char* p = s.data();
        (void)p[4];  // OK
    }
}

int main() {
    return 0;
}
