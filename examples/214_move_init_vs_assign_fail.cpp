// WHAT IS TESTED:
// After move, source should be invalidated (unknown state).
//
// Expected: ERROR

#include <utility>

struct Buffer {
    int _size;
};

[[clang::annotate("dtt:requires=_size > 0")]]
void needsNonEmpty(Buffer& b);

// Using moved-from source should fail
void test_use_after_move() {
    Buffer a;
    a._size = 10;
    Buffer b = std::move(a);
    needsNonEmpty(a);  // ERROR: a is moved-from, _size unknown  dtt-error {{arg0\._size > 0}}
}

int main() { test_use_after_move(); return 0; }
