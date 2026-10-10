// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 859_string_operator_plus_fail.cpp
// Tests that DTT catches out-of-bounds after operator+=(char)

#include <string>

void test_out_of_bounds_after_char() {
    std::string s;
    s += 'a';  // _size = 1
    s[2];  // ERROR: read beyond the null terminator  // dtt-error {{_size}}
}

void test_front_on_empty() {
    std::string s;
    s.front();  // ERROR: size == 0  // dtt-error {{_size}}
}

int main() { return 0; }
