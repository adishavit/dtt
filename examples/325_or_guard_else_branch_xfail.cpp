// WHAT IS TESTED:
// OR guards don't yield proofs in the else branch.
//
// Expected behavior: PASS (else implies ptr != nullptr)
// Current behavior: FAIL (nullability unknown)

struct Data {
    int value;
};

void test(Data* ptr, bool flag) {
    if (ptr == nullptr || flag) {
        return;
    }

    ptr->value = 1;
}

int main() {
    Data d;
    test(&d, false);
    return 0;
}
