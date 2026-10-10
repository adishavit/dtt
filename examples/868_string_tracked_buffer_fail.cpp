// Only dtt-error markers assert coverage; KNOWN GAP / UNTRACKED cases below do not.
// 868_string_tracked_buffer_fail.cpp
// Tests that DTT catches out-of-bounds with tracked buffer concatenation

#include <string>

void test_append_out_of_bounds() {
    std::string src;
    src.push_back('a');
    src.push_back('b');  // src._size = 2

    std::string dst;
    dst.append(src);  // dst._size = 2
    dst[3];  // ERROR: read beyond the null terminator  // dtt-error {{_size}}
}

void test_plus_equals_out_of_bounds() {
    std::string src;
    src += 'x';
    src += 'y';
    src += 'z';  // src._size = 3

    std::string dst;
    dst += src;  // dst._size = 3
    dst[4];  // ERROR: read beyond the null terminator  // dtt-error {{_size}}
}

void test_compound_out_of_bounds() {
    std::string a;
    a.push_back('1');  // a._size = 1

    std::string b;
    b += 'x';
    b += 'y';  // b._size = 2

    std::string c;
    c += a;  // c._size = 1
    c += b;  // c._size = 3
    c[4];  // ERROR: read beyond the null terminator  // dtt-error {{_size}}
}

int main() { return 0; }
