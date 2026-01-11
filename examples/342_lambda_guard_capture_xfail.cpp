// WHAT IS TESTED:
// Lambda guards are not analyzed, so proofs inside lambda bodies are missed.
//
// Expected behavior: PASS (guard in lambda should allow front())
// Current behavior: FAIL (lambda body not traversed)

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }
};

void test(Vec& v) {
    auto check = [&]() {
        if (v._size > 0) {
            v.front();
        }
    };
    check();
}

int main() {
    Vec v;
    v._size = 1;
    test(v);
    return 0;
}
