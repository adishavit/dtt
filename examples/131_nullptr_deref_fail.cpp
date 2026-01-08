// Example 131: nullptr dereference - should FAIL
// Tests that dereferencing a known-null pointer is caught

struct Data {
    int value;
};

int main() {
    Data* ptr = nullptr;
    ptr->value = 42;  // ERROR: ptr is known to be null

    return 0;
}
