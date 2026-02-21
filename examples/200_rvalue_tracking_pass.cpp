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
    int arr[10] = {};
    Buffer a;
    a.data = arr;
    a._size = 10;
    Buffer b = std::move(a);
    process(b);  // b._size = 10
}

// 2. Move assignment transfers properties
void test_move_assign() {
    int arr[5] = {};
    Buffer a;
    a.data = arr;
    a._size = 5;
    Buffer b;
    b.data = nullptr;
    b = std::move(a);
    process(b);  // b._size = 5
}

// 3. Direct constructor move
void test_direct_move() {
    int arr[3] = {};
    Buffer a;
    a.data = arr;
    a._size = 3;
    Buffer b(std::move(a));
    process(b);  // b._size = 3
}

// 4. Chain of moves
void test_move_chain() {
    int arr[7] = {};
    Buffer a;
    a.data = arr;
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
