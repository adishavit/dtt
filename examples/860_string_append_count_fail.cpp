// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 860_string_append_count_fail.cpp
// Tests that DTT catches out-of-bounds after append(count, char)

#include <string>

void test_out_of_bounds_after_append() {
    std::string s;
    s.append(3, 'x');  // _size = 3
    s[4];  // ERROR: read beyond the null terminator  // dtt-error {{_size}}
}

void test_compound_out_of_bounds() {
    std::string s;
    s.append(2, 'a');  // _size = 2
    s.append(3, 'b');  // _size = 5
    s[6];  // ERROR: read beyond the null terminator  // dtt-error {{_size}}
}

int main() { return 0; }
