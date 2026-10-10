// WHAT IS TESTED:
// Using moved-from object should fail.
//
// Expected: ERROR

#include <utility>

struct Buffer {
    int* data;
    int _size;
};

[[clang::annotate("dtt:requires=_size > 0")]]
void process(Buffer& b);

void test_use_after_move() {
    Buffer a;
    a._size = 10;
    Buffer b = std::move(a);
    process(a);  // ERROR: a is moved-from  dtt-error {{arg0\._size > 0}}
}

int main() { test_use_after_move(); return 0; }
