// Tests: Guards on pointer aliases transfer non-null proofs
// Issue #45: FIXED - alias = ptr; if (ptr) { alias->x; } now works

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
