// WHAT IS TESTED:
// Guard proofs from an if-statement without an else should NOT remain active
// after the statement. This test demonstrates the current unsound behavior:
// DTT keeps the proof, so the out-of-guard access is not diagnosed.
//
// Expected behavior: ERROR (front() requires _size > 0)
// Current behavior: SILENT (proof leaks past if)

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }
};

void test() {
    Vec v;
    v._size = 0;

    if (v._size > 0) {
        // no-op
    }

    // Should be unsafe: _size could still be 0
    v.front();
}

int main() { test(); return 0; }
