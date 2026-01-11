// WHAT IS TESTED:
// Guards using a struct field index are not recognized as var-bound proofs.
//
// Expected behavior: PASS (s.i < _size should allow access)
// Current behavior: FAIL (guard not recognized)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

struct State {
    int i;
};

void test(State s) {
    Vec v;
    v._size = 3;

    if (s.i < v._size) {
        v[s.i] = 1;
    }
}

int main() {
    State s{1};
    test(s);
    return 0;
}
