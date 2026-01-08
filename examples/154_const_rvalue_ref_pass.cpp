// Example 154: Const rvalue reference should NOT invalidate — should COMPILE
//
// const T&& is rare but legal. Since it's const, the function cannot
// modify the argument, so properties should remain valid.

#include <utility>

struct Vec {
    int* data;
    int _size;
};

// Takes const rvalue ref - cannot modify, should NOT invalidate
void observe(const Vec&& v);

[[clang::annotate("dtt:requires=_size > 0")]]
void process(Vec& v);

void test() {
    Vec v;
    v._size = 10;

    observe(std::move(v));  // const T&& - cannot modify v

    process(v);  // Should PASS: v._size still known to be 10
}

int main() { test(); return 0; }
