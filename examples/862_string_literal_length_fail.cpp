// 862_string_literal_length_fail.cpp
// Tests that DTT catches out-of-bounds with string literal length detection

#include <string>

void test_out_of_bounds_literal() {
    std::string s;
    s += "hello";  // _size = 5
    s[5];  // ERROR: index 5 >= size 5
}

void test_compound_out_of_bounds() {
    std::string s;
    s += "ab";   // _size = 2
    s += "cd";   // _size = 4
    s[4];  // ERROR: index 4 >= size 4
}

int main() { return 0; }
