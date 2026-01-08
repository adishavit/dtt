// Example 157: Non-const pointer param should invalidate — should FAIL
//
// Functions taking non-const pointers can modify the pointed-to object.
// This should trigger invalidation just like non-const refs.

struct Vec {
    int* data;
    int _size;
};

// Takes non-const pointer - can modify *v
void maybe_modify(Vec* v);

// Takes const pointer - cannot modify *v
void just_read(const Vec* v);

[[clang::annotate("dtt:requires=_size > 0")]]
void process(Vec& v);

void test() {
    Vec v1, v2;
    v1._size = 10;
    v2._size = 10;

    just_read(&v1);    // const pointer - v1 preserved
    maybe_modify(&v2); // non-const pointer - v2 invalidated

    process(v1);  // Should PASS: v1._size still 10
    process(v2);  // Should FAIL: v2._size unknown after maybe_modify
}

int main() { test(); return 0; }
