// WHAT IS TESTED:
// Guards on assignment expressions are not recognized.
//
// Expected behavior: PASS (assigned pointer checked for null)
// Current behavior: FAIL (no proof from assignment guard)

struct Data {
    int value;
};

void test(Data* ptr) {
    Data* local = nullptr;
    if ((local = ptr) != nullptr) {
        local->value = 1;
    }
}

int main() {
    Data d;
    test(&d);
    return 0;
}
