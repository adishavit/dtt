// 980_dimensional_xfail.cpp
// Tests: Dimensional analysis - adding meters to seconds caught
// From BLOG.md: Dimensional Analysis example
// XFAIL: Cross-argument field comparison (_dim_m == arg0._dim_m) not yet supported

struct Quantity {
    int _dim_m;  // meters exponent
    int _dim_s;  // seconds exponent

    [[clang::annotate("dtt:requires=_dim_m == arg0._dim_m")]]
    [[clang::annotate("dtt:requires=_dim_s == arg0._dim_s")]]
    Quantity& operator+=(const Quantity& other) { return *this; }
};

void test_add_incompatible() {
    Quantity dist;
    dist._dim_m = 1;
    dist._dim_s = 0;

    Quantity time;
    time._dim_m = 0;
    time._dim_s = 1;

    dist += time;  // ERROR: _dim_m(1) != _dim_m(0)
}

int main() {
    test_add_incompatible();
    return 0;
}
