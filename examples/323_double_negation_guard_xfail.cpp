// WHAT IS TESTED:
// Double-negation null guard isn't recognized.
//
// Expected behavior: PASS (ptr is non-null inside the branch)
// Current behavior: FAIL (nullability unknown)

struct Data {
    int value;
};

void test(Data* ptr) {
    if (!(ptr == nullptr)) {
        ptr->value = 1;
    }
}

int main() {
    Data d;
    test(&d);
    return 0;
}
