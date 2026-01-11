// WHAT IS TESTED:
// Guard proofs from an if-statement without an else should NOT remain active
// after the statement. DTT correctly catches this: the proof from the if-guard
// does not leak past the closing brace, so v.front() is correctly flagged.
//
// Expected: ERROR (v._size = 0 at call site, requires _size > 0)

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
