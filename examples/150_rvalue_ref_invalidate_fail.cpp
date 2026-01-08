// Example 150: Rvalue reference should invalidate — should FAIL
//
// BUG: isModifiableRef() only checks lvalue refs (T&) and pointers,
// but ignores rvalue refs (T&&). Functions taking T&& can modify
// the argument (sink parameters, move semantics) but DTT doesn't
// invalidate properties, leading to unsound proofs.
//
// After fix: This test should produce a DTT error because v._size
// is UNKNOWN after being moved from.

#include <utility>  // std::move

struct Vec {
    int* data;
    int _size;
};

// Sink function - consumes the vector via rvalue ref
void consume(Vec&& v);  // No postcondition, takes T&&

[[clang::annotate("dtt:requires=_size > 0")]]
void needsNonEmpty(Vec& v);

void test() {
    Vec v;
    v._size = 10;  // Known: v._size = 10

    consume(std::move(v));  // BUG: v._size NOT invalidated (T&& ignored)

    // Should FAIL: v._size is UNKNOWN after move
    // BUG: Currently PASSES because rvalue refs not detected
    needsNonEmpty(v);
}

int main() { test(); return 0; }
