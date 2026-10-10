// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 864_string_append_literal_fail.cpp
// Tests that DTT catches out-of-bounds with append string literal

#include <string>

void test_out_of_bounds_append() {
    std::string s;
    s.append("hello");  // _size = 5
    s[6];  // ERROR: read beyond the null terminator  // dtt-error {{_size}}
}

int main() { return 0; }
