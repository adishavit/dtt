// Example 152: Non-void function with ref param should invalidate — should FAIL
//
// BUG: Property invalidation for unannotated functions is gated to
// void return types (line 3075: Callee->getReturnType()->isVoidType()).
// A function returning a value but mutating a non-const ref/pointer
// leaves tracked properties untouched, producing unsound proofs.
//
// After fix: This test should produce a DTT error because v._size
// is UNKNOWN after pop_front() which mutates via ref but returns int.

struct Vec {
    int* data;
    int _size;
};

// Returns the popped element, but ALSO mutates v (decrements _size)
// Common pattern: int pop(Container&)
int pop_front(Vec& v);  // Returns int, no postcondition

[[clang::annotate("dtt:requires=_size >= 10")]]
void needsTen(Vec& v);

void test() {
    Vec v;
    v._size = 15;  // Known: v._size = 15

    int x = pop_front(v);  // State change checked by the subsequent requirement assertion.
                           // Actually decrements _size

    // Should FAIL: v._size is UNKNOWN after pop_front()
    // BUG: Currently PASSES because only void functions trigger invalidation
    needsTen(v);  // dtt-error {{arg0\._size >= 10}}
}

int main() { test(); return 0; }
