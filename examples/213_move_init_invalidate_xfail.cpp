// XFAIL: Move initialization should invalidate source
// When implemented, this will become 213_move_init_invalidate_fail.cpp
//
// EXPECTED BEHAVIOR:
// Buffer b = std::move(a) should invalidate 'a', making subsequent
// access to a._size produce a DTT error.
//
// CURRENT: Move init not tracked, no error emitted (false negative)
// FIXED:   Move init invalidates source, error emitted (test passes)

#include <utility>

struct Buffer {
    int _size;
};

[[clang::annotate("dtt:requires=_size > 0")]]
void process(Buffer& b);

void test() {
    Buffer a;
    a._size = 10;

    Buffer b = std::move(a);  // Should invalidate 'a'

    process(a);  // Should ERROR: a._size = ? (moved-from)
}
