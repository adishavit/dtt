// WHAT IS TESTED:
// Guards on casted pointers don't propagate non-null proof.
//
// Expected behavior: PASS (casted pointer is non-null inside guard)
// Current behavior: FAIL (no proof from casted guard)

struct Data {
    int value;
};

void test(void* raw) {
    if (static_cast<Data*>(raw) != nullptr) {
        static_cast<Data*>(raw)->value = 1;
    }
}

int main() {
    Data d;
    test(&d);
    return 0;
}
