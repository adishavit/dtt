// WHAT IS TESTED:
// Guards on index assignments (i = expr) aren't used to prove bounds.
//
// Expected behavior: PASS (i set under guard implies bounds)
// Current behavior: FAIL (no proof after assignment)

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

void test(int i) {
    Vec v;
    v._size = 3;

    if (i < v._size) {
        int j = i;
        v[j] = 1;
    }
}

int main() { test(1); return 0; }
