// 858_string_operator_plus_pass.cpp
// Tests string operator+=(char) - should increment size by 1

#include <string>

void test_plus_equals_char() {
    std::string s;
    s += 'a';  // _size = 1
    s.front();  // OK: size >= 1
}

void test_multiple_chars() {
    std::string s;
    s += 'a';  // _size = 1
    s += 'b';  // _size = 2
    s += 'c';  // _size = 3
    s[2];  // OK: index 2 < size 3
}

void test_mixed_push_and_plus() {
    std::string s;
    s.push_back('x');  // _size = 1
    s += 'y';          // _size = 2
    s.push_back('z');  // _size = 3
    s[2];  // OK: index 2 < size 3
}

int main() { return 0; }
