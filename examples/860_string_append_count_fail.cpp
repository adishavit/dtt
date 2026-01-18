// 860_string_append_count_fail.cpp
// Tests that DTT catches out-of-bounds after append(count, char)

#include <string>

void test_out_of_bounds_after_append() {
    std::string s;
    s.append(3, 'x');  // _size = 3
    s[3];  // ERROR: index 3 >= size 3
}

void test_compound_out_of_bounds() {
    std::string s;
    s.append(2, 'a');  // _size = 2
    s.append(3, 'b');  // _size = 5
    s[5];  // ERROR: index 5 >= size 5
}

int main() { return 0; }
