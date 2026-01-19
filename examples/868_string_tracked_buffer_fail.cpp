// 868_string_tracked_buffer_fail.cpp
// Tests that DTT catches out-of-bounds with tracked buffer concatenation

#include <string>

void test_append_out_of_bounds() {
    std::string src;
    src.push_back('a');
    src.push_back('b');  // src._size = 2

    std::string dst;
    dst.append(src);  // dst._size = 2
    dst[2];  // ERROR: index 2 >= size 2
}

void test_plus_equals_out_of_bounds() {
    std::string src;
    src += 'x';
    src += 'y';
    src += 'z';  // src._size = 3

    std::string dst;
    dst += src;  // dst._size = 3
    dst[3];  // ERROR: index 3 >= size 3
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
    c[3];  // ERROR: index 3 >= size 3
}

int main() { return 0; }
