// WHAT IS TESTED:
// Guards on pointer aliases don't transfer non-null proofs to aliases.
//
// Expected behavior: PASS (alias non-null inside guard)
// Current behavior: FAIL (alias nullability unknown)
//
// NOTE: Uses external function to ensure ghost state cannot infer nullability

struct Data {
    int value;
};

Data* getExternalData();  // Unknown nullability - prevents ghost state inference

void test(Data* ptr) {
    Data* alias = ptr;
    if (ptr != nullptr) {
        alias->value = 1;
    }
}

int main() {
    test(getExternalData());  // Pass unknown nullability
    return 0;
}
