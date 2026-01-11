// WHAT IS TESTED:
// Guards on function return values don't propagate proofs.
//
// Expected behavior: PASS (checkSize returns _size)
// Current behavior: FAIL (no proof from return value guard)

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }
};

[[clang::annotate("dtt:returns=_size")]]
int sizeOf(const Vec& v) {
    return v._size;
}

void test() {
    Vec v;
    v._size = 1;

    if (sizeOf(v) > 0) {
        v.front();
    }
}

int main() { test(); return 0; }
