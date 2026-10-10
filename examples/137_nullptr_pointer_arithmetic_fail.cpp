// WHAT IS TESTED:
// Null pointer arithmetic should be caught.
//
// Expected: ERROR

struct Data { int value; };

void test_null_arithmetic() {
    Data* ptr = nullptr;
    Data* ptr2 = ptr + 5;  // ptr2 still has null base
    ptr2->value = 1;       // ERROR: null-derived pointer  dtt-error {{potential null pointer dereference}}
}

int main() { test_null_arithmetic(); return 0; }
