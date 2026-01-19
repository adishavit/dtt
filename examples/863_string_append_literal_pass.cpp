// 863_string_append_literal_pass.cpp
// Tests string append() with literal length detection

#include <string>

void test_append_literal() {
    std::string s;
    s.append("hello");  // _size = 5
    s[4];  // OK: index 4 < size 5
}

void test_append_multiple_literals() {
    std::string s;
    s.append("ab");   // _size = 2
    s.append("cde");  // _size = 5
    s[4];  // OK: index 4 < size 5
}

void test_mixed_append() {
    std::string s;
    s.push_back('x');   // _size = 1
    s.append("yz");     // _size = 3
    s[2];  // OK: index 2 < size 3
}

int main() { return 0; }
