// WHAT IS TESTED:
// Guards on function return values should propagate proofs.
//
// Expected: PASS

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

[[clang::annotate("dtt:returns=_size")]]
int getLen(const Vec& v) {
    return v._size;
}

// 1. Simple return value guard
void test_simple_guard() {
    int arr[] = {1};
    Vec v;
    v.data = arr;
    v._size = 1;
    if (sizeOf(v) > 0) {
        v.front();
    }
}

// 2. Multiple return value guards
void test_multiple_guards() {
    int arr[] = {1, 2, 3, 4, 5};
    Vec v;
    v.data = arr;
    v._size = 5;
    if (sizeOf(v) > 0) {
        v.front();
    }
    if (getLen(v) > 0) {
        v.front();
    }
}

// 3. Guard in while loop
void test_while_guard() {
    int arr[] = {1, 2, 3};
    Vec v;
    v.data = arr;
    v._size = 3;
    while (sizeOf(v) > 0) {
        v.front();
        break;  // Avoid infinite loop
    }
}

// 4. Known size directly
void test_known_size() {
    int arr[10];
    Vec v;
    v.data = arr;
    v._size = 10;
    v.front();  // _size known to be 10 > 0
}

int main() {
    test_simple_guard();
    test_multiple_guards();
    test_while_guard();
    test_known_size();
    return 0;
}
