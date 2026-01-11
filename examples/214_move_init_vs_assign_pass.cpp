// WHAT IS TESTED:
// Move initialization should transfer properties like move assignment does.
//
// Expected: PASS

#include <utility>

struct Buffer {
    int _size;
};

[[clang::annotate("dtt:requires=_size > 0")]]
void needsNonEmpty(Buffer& b);

// Move ASSIGNMENT works
void test_move_assignment() {
    Buffer a1;
    a1._size = 10;
    Buffer b1;
    b1 = std::move(a1);
    needsNonEmpty(b1);  // b1._size = 10
}

// Move INITIALIZATION should also work
void test_move_initialization() {
    Buffer a2;
    a2._size = 20;
    Buffer b2 = std::move(a2);
    needsNonEmpty(b2);  // b2._size should be 20
}

// Direct constructor syntax
void test_move_direct_init() {
    Buffer a3;
    a3._size = 30;
    Buffer b3(std::move(a3));
    needsNonEmpty(b3);  // b3._size should be 30
}

int main() {
    test_move_assignment();
    test_move_initialization();
    test_move_direct_init();
    return 0;
}
