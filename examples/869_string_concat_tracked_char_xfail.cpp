// 869_string_concat_tracked_char_xfail.cpp
// GAP: std::string += char[] - C string length unknown at compile time
//
// DTT cannot determine actual string length in a char array because:
// - C arrays have declared size from type (e.g., char[10] -> size 10)
// - But \0 terminator can be anywhere (actual strlen unknown)
// - Tracking writes to detect \0 position would require new feature
//
// Current behavior: tracking invalidated, bounds checking disabled
// Future: could track \0 writes to determine actual string length

#include <string>

void test_string_plus_equals_tracked_char_array() {
    char buf[10];
    buf[0] = 'a';
    buf[1] = 'b';
    buf[2] = '\0';  // Actual string length is 2

    std::string s;
    s += buf;  // DTT should set s._size = 2, but can't determine this
    s[2];  // GAP: Should be ERROR (index 2 >= size 2), but tracking lost
}

void test_string_append_tracked_char_array() {
    char buf[20];
    buf[0] = 'x';
    buf[1] = 'y';
    buf[2] = 'z';
    buf[3] = '\0';  // Actual string length is 3

    std::string s;
    s.append(buf);  // DTT should set s._size = 3, but can't determine this
    s[3];  // GAP: Should be ERROR (index 3 >= size 3), but tracking lost
}

int main() { return 0; }
