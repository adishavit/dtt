// 857_string_append_count_pass.cpp
// Tests string append(count, char) annotation

#include <string>

void test_append_count() {
    std::string s;
    s.append(5, 'x');  // _size = 0 + 5 = 5
    s[4];  // OK: index 4 < size 5
}

void test_append_count_after_push() {
    std::string s;
    s.push_back('a');  // _size = 1
    s.append(3, 'b');  // _size = 1 + 3 = 4
    s[3];  // OK: index 3 < size 4
}

void test_append_multiple() {
    std::string s;
    s.append(2, 'a');  // _size = 2
    s.append(3, 'b');  // _size = 2 + 3 = 5
    s[4];  // OK: index 4 < size 5
}

int main() { return 0; }
