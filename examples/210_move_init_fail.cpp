// Example 210: Move initialization invalidates source — should FAIL
// DTT correctly tracks move construction and invalidates the source.

#include <utility>

struct Buffer {
    int* data;
    int _size;
};

[[clang::annotate("dtt:requires=_size > 0")]]
void process(Buffer& b);

void test() {
    Buffer a;
    a._size = 10;

    Buffer b = std::move(a);  // a is now moved-from, b._size = 10

    process(a);  // ERROR: a._size = ? (invalidated by move)  dtt-error {{arg0\._size > 0}}
}

int main() { test(); return 0; }
