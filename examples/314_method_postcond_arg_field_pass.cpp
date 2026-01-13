// WHAT IS TESTED:
// Method postconditions that reference arg0._field are properly substituted.
// Issue #7: FIXED - arg0._size now substituted with actual argument field value

struct Vec {
    int* data;
    int _size;
    int _capacity;

    [[clang::annotate("dtt:requires=_size > 1")]]
    int& at1() { return data[1]; }

    [[clang::annotate("dtt:requires=_size > 2")]]
    int& at2() { return data[2]; }
};

// Variant 1: Basic arg0._size substitution
struct Container1 {
    int _size;

    [[clang::annotate("dtt:ensures=_size == _size + arg0._size")]]
    void append(const Vec& other) { (void)other; }
};

void variant1_basic() {
    Container1 c;
    c._size = 1;
    Vec v;
    v._size = 2;
    c.append(v);  // c._size = 1 + 2 = 3
    // c._size is now 3, but we need a way to verify...
}

// Variant 2: Multiple argument fields in postcondition
struct Container2 {
    int _size;
    int _capacity;

    [[clang::annotate("dtt:ensures=_size == arg0._size")]]
    [[clang::annotate("dtt:ensures=_capacity == arg0._capacity")]]
    void copyFrom(const Vec& src) { (void)src; }

    [[clang::annotate("dtt:requires=_size > 1")]]
    void needsTwo() {}
};

void variant2_multiple_fields() {
    Container2 c;
    c._size = 0;
    c._capacity = 0;
    Vec v;
    v._size = 5;
    v._capacity = 10;
    c.copyFrom(v);  // c._size = 5, c._capacity = 10
    c.needsTwo();   // OK: _size = 5 > 1
}

// Variant 3: Chained method calls with arg fields
struct Builder {
    int _count;

    [[clang::annotate("dtt:ensures=_count == _count + arg0._size")]]
    void addVec(const Vec& v) { (void)v; }

    [[clang::annotate("dtt:requires=_count > 2")]]
    void build() {}
};

void variant3_chained() {
    Builder b;
    b._count = 0;
    Vec v1, v2;
    v1._size = 1;
    v2._size = 2;
    b.addVec(v1);  // _count = 0 + 1 = 1
    b.addVec(v2);  // _count = 1 + 2 = 3
    b.build();     // OK: _count = 3 > 2
}

int main() {
    variant1_basic();
    variant2_multiple_fields();
    variant3_chained();
    return 0;
}
