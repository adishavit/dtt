// WHAT IS TESTED:
// Void postconditions with argument expressions are properly substituted.
// Issue #26: FIXED - arg1 + 1, arg1 * 2, etc. now evaluated correctly

struct Vec {
    int* data;
    int _size;

    [[clang::annotate("dtt:requires=_size > 1")]]
    int& at1() { return data[1]; }

    [[clang::annotate("dtt:requires=_size > 4")]]
    int& at4() { return data[4]; }

    [[clang::annotate("dtt:requires=_size > 9")]]
    int& at9() { return data[9]; }
};

// Variant 1: arg1 + constant
[[clang::annotate("dtt:ensures=result._size == arg1 + 1")]]
void initPlusOne(Vec& v, int count) {
    (void)v; (void)count;
}

void variant1_plus_constant() {
    Vec v;
    v._size = 0;
    initPlusOne(v, 4);  // v._size = 4 + 1 = 5
    v.at4();            // OK: _size = 5 > 4
}

// Variant 2: arg1 * constant
[[clang::annotate("dtt:ensures=result._size == arg1 * 2")]]
void initDouble(Vec& v, int count) {
    (void)v; (void)count;
}

void variant2_multiply() {
    Vec v;
    v._size = 0;
    initDouble(v, 5);  // v._size = 5 * 2 = 10
    v.at9();           // OK: _size = 10 > 9
}

// Variant 3: arg1 - constant
[[clang::annotate("dtt:ensures=result._size == arg1 - 1")]]
void initMinusOne(Vec& v, int count) {
    (void)v; (void)count;
}

void variant3_minus() {
    Vec v;
    v._size = 0;
    initMinusOne(v, 6);  // v._size = 6 - 1 = 5
    v.at4();             // OK: _size = 5 > 4
}

// Variant 4: Complex expression with multiple args
[[clang::annotate("dtt:ensures=result._size == arg1 + arg2 + 1")]]
void initComplex(Vec& v, int a, int b) {
    (void)v; (void)a; (void)b;
}

void variant4_complex_expr() {
    Vec v;
    v._size = 0;
    initComplex(v, 2, 2);  // v._size = 2 + 2 + 1 = 5
    v.at4();               // OK: _size = 5 > 4
}

// Variant 5: Expression with arg0._field
struct Source {
    int _count;
};

[[clang::annotate("dtt:ensures=result._size == arg1._count + 1")]]
void initFromSource(Vec& v, const Source& src) {
    (void)v; (void)src;
}

void variant5_arg_field_expr() {
    Vec v;
    v._size = 0;
    Source s;
    s._count = 4;
    initFromSource(v, s);  // v._size = 4 + 1 = 5
    v.at4();               // OK: _size = 5 > 4
}

int main() {
    variant1_plus_constant();
    variant2_multiply();
    variant3_minus();
    variant4_complex_expr();
    variant5_arg_field_expr();
    return 0;
}
