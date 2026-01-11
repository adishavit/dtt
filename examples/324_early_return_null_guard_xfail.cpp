// WHAT IS TESTED:
// Early-return null guard doesn't propagate non-null after the return.
//
// Expected behavior: PASS (ptr is non-null after early return)
// Current behavior: FAIL (nullability unknown)

struct Data {
    int value;
};

void test(Data* ptr) {
    if (!ptr) {
        return;
    }

    ptr->value = 1;
}

int main() {
    Data d;
    test(&d);
    return 0;
}
