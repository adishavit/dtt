// 859_string_operator_plus_fail.cpp
// Tests that DTT catches out-of-bounds after operator+=(char)

#include <string>

void test_out_of_bounds_after_char() {
    std::string s;
    s += 'a';  // _size = 1
    s[1];  // ERROR: index 1 >= size 1
}

void test_front_on_empty() {
    std::string s;
    s.front();  // ERROR: size == 0
}

int main() { return 0; }
