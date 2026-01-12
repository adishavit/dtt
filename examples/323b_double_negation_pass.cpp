// WHAT IS TESTED:
// Without the guard, pointer access should still be caught.
//
// Expected: ERROR (ptr could be null without guard)

struct Data {
    int value;
};

void test(Data* ptr) {
    // No guard - should fail
    ptr->value = 1;
}

int main() {
    Data d;
    test(&d);
    return 0;
}
