// Example 134: nullptr check else branch - should FAIL
// Tests that the else branch of a null check is known to be null

struct Data {
    int value;
};

int main() {
    Data* ptr = nullptr;

    if (ptr != nullptr) {
        ptr->value = 42;  // OK: guarded
    } else {
        ptr->value = 10;  // ERROR: ptr is known null in else branch  dtt-error {{potential null pointer dereference}}
    }

    return 0;
}
