// 861_string_literal_length_pass.cpp
// Tests string literal length detection via constexpr strlen

#include <string>

void test_operator_plus_literal() {
    std::string s;
    s += "hello";  // _size = 5 (strlen("hello"))
    s[4];  // OK: index 4 < size 5
}

void test_append_literal() {
    std::string s;
    s.append("world");  // _size = 5
    s[4];  // OK: index 4 < size 5
}

void test_compound_literals() {
    std::string s;
    s += "ab";      // _size = 2
    s += "cde";     // _size = 5
    s[4];  // OK: index 4 < size 5
}

void test_mixed_char_and_literal() {
    std::string s;
    s += 'a';       // _size = 1
    s += "bc";      // _size = 3
    s[2];  // OK: index 2 < size 3
}

void test_empty_literal() {
    std::string s;
    s.push_back('x');  // _size = 1
    s += "";           // _size = 1 (empty string adds 0)
    s[0];  // OK: index 0 < size 1
}

int main() { return 0; }
