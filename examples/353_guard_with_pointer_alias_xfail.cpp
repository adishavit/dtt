// WHAT IS TESTED:
// Guards on pointer aliases don't transfer non-null proofs to aliases.
//
// Expected behavior: PASS (alias non-null inside guard)
// Current behavior: FAIL (alias nullability unknown)

struct Data {
    int value;
};

void test(Data* ptr) {
    Data* alias = ptr;
    if (ptr != nullptr) {
        alias->value = 1;
    }
}

int main() {
    Data d;
    test(&d);
    return 0;
}
