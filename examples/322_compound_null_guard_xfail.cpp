// WHAT IS TESTED:
// Compound null guard with short-circuit member access isn't recognized.
//
// Expected behavior: PASS (ptr is non-null inside the && branch)
// Current behavior: FAIL (nullability unknown)

struct Data {
    int value;
    bool ready;
};

void test() {
    Data* ptr = new Data();
    ptr->ready = true;

    if (ptr && ptr->ready) {
        ptr->value = 1;
    }
}

int main() { test(); return 0; }
