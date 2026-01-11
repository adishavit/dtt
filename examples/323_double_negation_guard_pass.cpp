// WHAT IS TESTED:
// Negation of null guards should be recognized and correctly inverted.
// !(ptr == nullptr) is semantically equivalent to (ptr != nullptr).
//
// Expected: PASS (all guards prove ptr is non-null)

struct Data {
    int value;
};

void test_double_negation(Data* ptr) {
    if (!(ptr == nullptr)) {
        ptr->value = 1;  // Safe: !(ptr == nullptr) proves ptr != nullptr
    }
}

void test_triple_negation(Data* ptr) {
    if (!(!(ptr != nullptr))) {
        ptr->value = 1;  // Safe: triple negation proves ptr != nullptr
    }
}

int main() {
    Data d;
    test_double_negation(&d);
    test_triple_negation(&d);
    return 0;
}
