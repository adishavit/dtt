// Example 130: nullptr guard - should COMPILE
// Tests that null pointer checks create proofs that allow dereference

struct Data {
    int value;
};

int main() {
    Data* ptr = nullptr;

    // Guard with != nullptr
    if (ptr != nullptr) {
        ptr->value = 42;  // OK: guarded by null check
    }

    // Guard with implicit bool
    Data* ptr2 = nullptr;
    if (ptr2) {
        ptr2->value = 10;  // OK: guarded by truthiness check
    }

    // Guard with != 0
    Data* ptr3 = nullptr;
    if (ptr3 != 0) {
        ptr3->value = 20;  // OK: guarded by != 0
    }

    // Pointer from new is always non-null
    Data* ptr4 = new Data();
    ptr4->value = 30;  // OK: new always returns non-null
    delete ptr4;

    // Address-of is always non-null
    Data d;
    Data* ptr5 = &d;
    ptr5->value = 40;  // OK: &x is always non-null

    return 0;
}
