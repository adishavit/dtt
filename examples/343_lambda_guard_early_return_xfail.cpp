// WHAT IS TESTED:
// Early-return guards inside lambdas are ignored.
//
// Expected behavior: PASS (ptr non-null after early return)
// Current behavior: FAIL (lambda body skipped)

struct Data { int value; };

void test(Data* ptr) {
    auto use = [&]() {
        if (!ptr) return;
        ptr->value = 1;
    };
    use();
}

int main() {
    Data d;
    test(&d);
    return 0;
}
