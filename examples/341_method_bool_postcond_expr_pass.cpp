// WHAT IS TESTED:
// Method bool postconditions with expressions should work.
//
// Expected: PASS

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:ensures_if_true=_size >= 2")]]
    bool hasTwo() const { return _size >= 2; }

    [[clang::annotate("dtt:ensures_if_true=_size > 0")]]
    bool nonEmpty() const { return _size > 0; }

    [[clang::annotate("dtt:requires=_size > 1")]]
    int& at1() { return data[1]; }

    [[clang::annotate("dtt:requires=_size > 0")]]
    int& front() { return data[0]; }
};

// 1. ensures_if_true with >= expression
void test_has_two() {
    Vec v;
    v._size = 2;
    if (v.hasTwo()) {
        v.at1();  // _size >= 2 implies _size > 1
    }
}

// 2. ensures_if_true with > expression
void test_non_empty() {
    Vec v;
    v._size = 1;
    if (v.nonEmpty()) {
        v.front();  // _size > 0
    }
}

// 3. Chained bool checks
void test_chained() {
    Vec v;
    v._size = 3;
    if (v.nonEmpty() && v.hasTwo()) {
        v.at1();
    }
}

// 4. Known value (no bool check needed)
void test_known() {
    Vec v;
    v._size = 5;
    v.at1();  // _size = 5 > 1
}

int main() {
    test_has_two();
    test_non_empty();
    test_chained();
    test_known();
    return 0;
}
