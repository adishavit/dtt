// WHAT IS TESTED:
// Early-return doesn't apply when conditions don't guarantee non-null.
//
// Expected: ERROR

struct Data {
    int value;

    [[clang::annotate("dtt:requires=this != nullptr")]]
    void requiresNonNull() {}
};

Data* get_data();  // Unknown, may return null

// No early return - null access possible
void test_no_early_return() {
    Data* ptr = get_data();
    // No guard at all
    ptr->requiresNonNull();  // ERROR: ptr may be null
}

int main() { test_no_early_return(); return 0; }
