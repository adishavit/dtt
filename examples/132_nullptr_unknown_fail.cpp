// Example 132: unknown nullability dereference - should FAIL
// Tests that dereferencing a pointer with unknown nullability is caught

struct Data {
    int value;
};

Data* getPointer();  // Unknown if this returns null or not

int main() {
    Data* ptr = getPointer();
    ptr->value = 42;  // ERROR: ptr's nullability is unknown

    return 0;
}
