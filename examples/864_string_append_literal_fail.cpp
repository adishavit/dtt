// 864_string_append_literal_fail.cpp
// Tests that DTT catches out-of-bounds with append string literal

#include <string>

void test_out_of_bounds_append() {
    std::string s;
    s.append("hello");  // _size = 5
    s[5];  // ERROR: index 5 >= size 5
}

int main() { return 0; }
