// WHAT IS TESTED:
// Null dereference without proper guard should fail.
//
// Expected: ERROR

struct Data {
    int value;
};

void test_no_guard() {
    Data* ptr = nullptr;
    ptr->value = 1;  // ERROR: no guard, ptr is nullptr  dtt-error {{potential null pointer dereference}}
}

int main() { test_no_guard(); return 0; }
