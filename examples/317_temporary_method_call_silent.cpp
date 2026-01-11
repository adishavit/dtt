// WHAT IS TESTED:
// Method constraints are skipped when the implicit object is a temporary.
//
// Expected behavior: ERROR (constraints should be enforced on temporaries)
// Current behavior: SILENT (no object path for temporary)

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }

    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& at(int idx) { return data[idx]; }
};

Vec makeEmpty() {
    Vec v;
    v._size = 0;
    return v;
}

Vec makeSized(int n) {
    Vec v;
    v._size = n;
    return v;
}

void test_temporary_front() {
    makeEmpty().front();  // Should error: _size = 0 violates _size > 0
}

void test_temporary_at() {
    makeSized(3).at(5);  // Should error: 5 >= 3 violates arg0 < _size
}

// Note: Assigning temporary to variable and then calling method DOES error correctly.
// void test_chained_temporary() {
//     Vec v = makeEmpty();
//     v.front();  // This correctly errors (not a temporary)
// }

void test_inline_temporary() {
    Vec{nullptr, 0}.front();  // Should error: aggregate init temporary
}

int main() {
    test_temporary_front();
    test_temporary_at();
    // test_chained_temporary();  // Commented: this one correctly errors
    test_inline_temporary();
    return 0;
}
