// WHAT IS TESTED:
// Rvalue reference tracking - properties transfer on std::move().
//
// Expected: PASS

#include <utility>

struct Buffer {
    int* data;
    int _size;
};

[[clang::annotate("dtt:requires=_size > 0")]]
void process(Buffer& b);

// 1. Move initialization transfers properties
void test_move_init() {
    Buffer a;
    a._size = 10;
    Buffer b = std::move(a);
    process(b);  // b._size = 10
}

// 2. Move assignment transfers properties
void test_move_assign() {
    Buffer a;
    a._size = 5;
    Buffer b;
    b = std::move(a);
    process(b);  // b._size = 5
}

// 3. Direct constructor move
void test_direct_move() {
    Buffer a;
    a._size = 3;
    Buffer b(std::move(a));
    process(b);  // b._size = 3
}

// 4. Chain of moves
void test_move_chain() {
    Buffer a;
    a._size = 7;
    Buffer b = std::move(a);
    Buffer c = std::move(b);
    process(c);  // c._size = 7
}

int main() {
    test_move_init();
    test_move_assign();
    test_direct_move();
    test_move_chain();
    return 0;
}
