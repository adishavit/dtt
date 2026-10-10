// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 862_string_literal_length_fail.cpp
// Tests that DTT catches out-of-bounds with string literal length detection

#include <string>

void test_out_of_bounds_literal() {
    std::string s;
    s += "hello";  // _size = 5
    s[6];  // ERROR: read beyond the null terminator  // dtt-error {{_size}}
}

void test_compound_out_of_bounds() {
    std::string s;
    s += "ab";   // _size = 2
    s += "cd";   // _size = 4
    s[5];  // ERROR: read beyond the null terminator  // dtt-error {{_size}}
}

int main() { return 0; }
