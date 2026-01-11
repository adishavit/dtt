// WHAT IS TESTED:
// Guards on pointer member fields don't propagate non-null proofs.
//
// Expected behavior: PASS (ptr is non-null inside guard)
// Current behavior: FAIL (nullability unknown)

struct Data {
    int value;
};

struct Holder {
    Data* ptr;
};

void test(Holder h) {
    if (h.ptr != nullptr) {
        h.ptr->value = 1;
    }
}

int main() {
    Data d;
    Holder h{&d};
    test(h);
    return 0;
}
