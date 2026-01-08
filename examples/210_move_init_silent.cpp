// SILENT: Move initialization doesn't invalidate source — false negative
//
// WHAT IS TESTED:
// Move INITIALIZATION (Buffer b = std::move(a)) should invalidate source.
// Currently only move ASSIGNMENT (b = std::move(a)) is tracked.
//
// WHY IT'S DIFFICULT:
// Move initialization uses copy/move constructor, not operator=.
// DTT tracks CXXOperatorCallExpr for operator=, but initialization
// goes through CXXConstructExpr which isn't currently handled.
//
// CURRENT BEHAVIOR:
// - Move assignment: `b = std::move(a)` → invalidates a (works)
// - Move init: `Buffer b = std::move(a)` → a unchanged (false negative)
//
// TO FIX:
// Handle CXXConstructExpr with std::move arguments to invalidate source.

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

    // SILENT: Should ERROR but doesn't — move init not tracked
    process(a);  // BUG: Using moved-from 'a', but DTT doesn't know
}

int main() { test(); return 0; }
