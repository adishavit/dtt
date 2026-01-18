// 865_string_tracked_buffer_pass.cpp
// Tests string append/+= with tracked buffer (vector<char>, string)

#include <string>
#include <vector>

void test_append_from_string() {
    std::string src;
    src.push_back('a');
    src.push_back('b');  // src._size = 2

    std::string dst;
    dst.append(src);  // dst._size = src._size = 2
    dst[1];  // OK: index 1 < size 2
}

void test_plus_equals_string() {
    std::string src;
    src += 'x';
    src += 'y';
    src += 'z';  // src._size = 3

    std::string dst;
    dst += src;  // dst._size = 3
    dst[2];  // OK: index 2 < size 3
}

void test_compound_tracked() {
    std::string a;
    a.push_back('1');  // a._size = 1

    std::string b;
    b += 'x';
    b += 'y';  // b._size = 2

    std::string c;
    c += a;  // c._size = 1
    c += b;  // c._size = 3
    c[2];  // OK: index 2 < size 3
}

int main() { return 0; }
