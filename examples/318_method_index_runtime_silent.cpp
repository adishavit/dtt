// WHAT IS TESTED:
// Method arg0 < _size checks skip runtime index arguments.
//
// Expected behavior: ERROR (runtime args with unknown bounds should warn)
// Current behavior: SILENT (runtime arg not checked)

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& at(int idx) { return data[idx]; }

    [[clang::annotate("dtt:requires=arg0 >= 0 && arg0 < _size")]]
    int& safe_at(int idx) { return data[idx]; }
};

void test_runtime_param(int i) {
    Vec v;
    v._size = 3;
    v.at(i);  // Should warn: i is runtime, bounds unknown
}

void test_runtime_computed(int n) {
    Vec v;
    v._size = 3;
    v.at(n * 2);  // Should warn: n*2 is runtime expression
}

int get_index();  // External function, runtime value

void test_runtime_from_call() {
    Vec v;
    v._size = 3;
    int idx = get_index();  // Runtime value from external call
    v.at(idx);  // Should warn: idx bounds unknown
}

void test_loop_variable() {
    Vec v;
    v._size = 3;
    for (int i = 0; i < 10; ++i) {
        v.at(i);  // Should warn: loop goes beyond _size
    }
}

int main() {
    test_runtime_param(5);
    test_runtime_computed(2);
    test_runtime_from_call();
    test_loop_variable();
    return 0;
}
