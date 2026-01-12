// WHAT IS TESTED:
// Lambda guards are not analyzed, so proofs inside lambda bodies are missed.
//
// Expected behavior: PASS (guard in lambda should allow front())
// Current behavior: FAIL (lambda body not traversed)
//
// NOTE: Uses external function to ensure ghost state cannot infer size

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }
};

Vec& getExternalVec();  // Unknown size - prevents ghost state inference

void test(Vec& v) {
    auto check = [&]() {
        if (v._size > 0) {
            v.front();
        }
    };
    check();
}

int main() {
    test(getExternalVec());  // Pass unknown size
    return 0;
}
