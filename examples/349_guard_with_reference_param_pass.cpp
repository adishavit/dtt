// WHAT IS TESTED:
// Guards on reference parameters should work like value parameters.
//
// Expected: PASS

struct Vec {
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int idx) { return *reinterpret_cast<int*>(0); }
};

// 1. Value parameter
void test_value_param(int i) {
    Vec v;
    v._size = 3;
    if (i < v._size) {
        v[i] = 1;
    }
}

// 2. Reference parameter
void test_ref_param(int& i) {
    Vec v;
    v._size = 3;
    if (i < v._size) {
        v[i] = 1;
    }
}

// 3. Const reference parameter
void test_const_ref_param(const int& i) {
    Vec v;
    v._size = 3;
    if (i < v._size) {
        v[i] = 1;
    }
}

// 4. Rvalue reference parameter
void test_rvalue_ref_param(int&& i) {
    Vec v;
    v._size = 3;
    if (i < v._size) {
        v[i] = 1;
    }
}

// 5. Multiple reference params - separate guards
void test_multi_ref(int& i, int& j) {
    Vec v;
    v._size = 5;
    if (i < v._size) {
        v[i] = 1;
    }
    if (j < v._size) {
        v[j] = 2;
    }
}

// 6. Nested if with reference param
void test_nested_if(int& i) {
    Vec v;
    v._size = 5;
    if (i >= 0) {
        if (i < v._size) {
            v[i] = 1;
        }
    }
}

// 7. Reference to pointer param (pointer null check)
struct Data { int value; };

void test_ref_to_ptr(Data*& ptr) {
    if (ptr != nullptr) {
        ptr->value = 42;
    }
}

int main() {
    int x = 1;
    test_value_param(x);
    test_ref_param(x);
    test_const_ref_param(x);
    test_rvalue_ref_param(1);
    test_multi_ref(x, x);
    test_nested_if(x);
    Data d;
    Data* p = &d;
    test_ref_to_ptr(p);
    return 0;
}
