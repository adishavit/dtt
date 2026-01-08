// XFAIL: Accessing moved-from object — should FAIL but currently PASSES
//
// WHAT IS TESTED:
// After std::move(obj), accessing obj should produce a warning/error
// since it's in a valid-but-unspecified state.
//
// WHY IT'S DIFFICULT:
// 1. Need to track "moved-from" state separately from "unknown"
// 2. Some operations on moved-from objects are valid (assignment, destruction)
// 3. Standard allows moved-from objects to be reused after reassignment
// 4. Would need flow-sensitive "taint" tracking
//
// RATIONALE:
// Accessing moved-from objects is a common source of bugs:
// - Reading from moved-from string/vector gives empty or garbage
// - Using moved-from unique_ptr is UB
// - Moved-from state is valid but unspecified (can't rely on values)
//
// CURRENT BEHAVIOR:
// DTT invalidates moved-from objects (good) but doesn't distinguish
// "moved-from" from "unknown". Both allow access with "unknown" warning.
// Ideally, moved-from access would be a stronger error.

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

    Buffer b = std::move(a);  // a is now moved-from

    // XFAIL: Should ERROR with "accessing moved-from object"
    // Currently just warns about unknown _size
    process(a);  // BUG: Using moved-from 'a'
}

int main() { test(); return 0; }
