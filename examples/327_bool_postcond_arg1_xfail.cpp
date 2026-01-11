// WHAT IS TESTED:
// Boolean postconditions on arg1 are ignored in guard propagation.
//
// Expected behavior: PASS (guard should prove v._size > 0)
// Current behavior: FAIL (no proof for arg1)

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }
};

[[clang::annotate("dtt:ensures_if_true=arg1._size > 0")]]
bool isNonEmpty(int dummy, const Vec& v) {
    (void)dummy;
    return v._size > 0;
}

void test() {
    Vec v;
    v._size = 1;
    if (isNonEmpty(0, v)) {
        v.front();
    }
}

int main() { test(); return 0; }
