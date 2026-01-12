// WHAT IS TESTED:
// Early-return guards inside lambdas are ignored.
//
// Expected behavior: PASS (ptr non-null after early return)
// Current behavior: FAIL (lambda body skipped)
//
// NOTE: Uses external function to ensure ghost state cannot infer nullability

struct Data { int value; };

Data* getExternalData();  // Unknown nullability - prevents ghost state inference

void test(Data* ptr) {
    auto use = [&]() {
        if (!ptr) return;
        ptr->value = 1;
    };
    use();
}

int main() {
    test(getExternalData());  // Pass unknown nullability
    return 0;
}
