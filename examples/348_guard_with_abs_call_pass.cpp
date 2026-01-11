// WHAT IS TESTED:
// Guards using function calls on index should be recognized.
//
// Expected: PASS

int abs(int);
int my_clamp(int x, int lo, int hi);

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

// 1. abs() guard
void test_abs(int i) {
    Vec v;
    v._size = 3;
    if (abs(i) < v._size) {
        v[abs(i)] = 1;
    }
}

// 2. User function guard
void test_user_func(int i) {
    Vec v;
    v._size = 10;
    if (my_clamp(i, 0, 5) < v._size) {
        v[my_clamp(i, 0, 5)] = 1;
    }
}

// 3. Multiple abs calls
void test_multiple_abs(int i, int j) {
    Vec v;
    v._size = 5;
    if (abs(i) < v._size) {
        v[abs(i)] = 1;
    }
    if (abs(j) < v._size) {
        v[abs(j)] = 2;
    }
}

int main() {
    test_abs(1);
    test_user_func(3);
    test_multiple_abs(1, 2);
    return 0;
}
