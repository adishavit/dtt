// WHAT IS TESTED:
// OR guards don't yield proofs in the else branch.
//
// Expected behavior: PASS (else implies ptr != nullptr)
// Current behavior: FAIL (nullability unknown)
//
// NOTE: Uses external function to ensure ghost state cannot infer nullability

struct Data {
    int value;
};

Data* getExternalData();  // Unknown nullability - prevents ghost state inference

void test(Data* ptr, bool flag) {
    if (ptr == nullptr || flag) {
        return;
    }

    ptr->value = 1;
}

int main() {
    test(getExternalData(), false);  // Pass unknown nullability
    return 0;
}
