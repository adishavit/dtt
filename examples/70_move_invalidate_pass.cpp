// Example 70: Move semantics — moved-from object becomes Unknown — should COMPILE
// After move, source properties are Unknown so no DTT checking occurs

#include "../include/dtt_vector.h"
#include <utility>

void test_move_source_unknown() {
    dtt::vector<int> v1;
    v1.resize(5);  // v1._size = 5

    dtt::vector<int> v2;
    v2 = std::move(v1);  // v2._size = 5, v1._size = Unknown

    // v2 has known size, access is checked
    v2[4];  // OK: 4 < 5

    // v1 is moved-from, properties are Unknown
    // DTT does NOT check this — runtime will handle it
    // (We don't access v1 here to avoid runtime crash)
}

void test_move_preserves_destination() {
    dtt::vector<int> src;
    src.resize(10);  // src._size = 10

    dtt::vector<int> dst;
    dst = std::move(src);  // dst._size = 10

    dst[9];  // OK: 9 < 10
    dst.back();  // OK: _size > 0
}

int main() {
    test_move_source_unknown();
    test_move_preserves_destination();
    return 0;
}
