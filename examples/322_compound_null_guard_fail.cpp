// WHAT IS TESTED:
// Compound null guard should still catch null access outside guard.
//
// Expected: ERROR

struct Data {
    int value;
    bool ready;

    [[clang::annotate("dtt:requires=this != nullptr")]]
    void requiresNonNull() {}
};

Data* get_data();  // Unknown, may return null

// Access outside compound guard
void test_access_outside_guard() {
    Data* ptr = get_data();

    if (ptr && ptr->ready) {
        ptr->value = 1;  // OK inside guard
    }

    ptr->requiresNonNull();  // ERROR: ptr may be null here  dtt-error {{potential null pointer dereference}}
}

int main() { test_access_outside_guard(); return 0; }
