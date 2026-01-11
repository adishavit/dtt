// WHAT IS TESTED:
// Member-call boolean guards without annotations don't yield proofs.
//
// Expected behavior: PASS (bool guard should imply non-null)
// Current behavior: FAIL (no proof from unannotated call)

struct Data { int value; };

struct Holder {
    Data* ptr;

    bool hasValue() const { return ptr != nullptr; }
};

void test(Holder h) {
    if (h.hasValue()) {
        h.ptr->value = 1;
    }
}

int main() {
    Data d;
    Holder h{&d};
    test(h);
    return 0;
}
