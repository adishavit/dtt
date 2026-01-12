// WHAT IS TESTED:
// Guards on assignment expressions are not recognized.
//
// Expected behavior: PASS (assigned pointer checked for null)
// Current behavior: FAIL (no proof from assignment guard)
//
// NOTE: Uses external function to ensure ghost state cannot infer nullability

struct Data {
    int value;
};

Data* getExternalData();  // Unknown nullability - prevents ghost state inference

void test(Data* ptr) {
    Data* local = nullptr;
    if ((local = ptr) != nullptr) {
        local->value = 1;
    }
}

int main() {
    test(getExternalData());  // Pass unknown nullability
    return 0;
}
