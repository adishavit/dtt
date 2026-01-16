// WHAT IS TESTED:
// Simple requires constraint without parentheses - verifies basic constraint checking works.
//
// Expected: ERROR (constraints should be enforced)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int front() { return 0; }
};

void test_simple_requires() {
    Vec v;
    v._size = 0;
    v.front();  // Should error: _size = 0 violates _size > 0
}

int main() {
    test_simple_requires();
    return 0;
}
