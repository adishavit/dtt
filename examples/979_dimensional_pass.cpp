// 979_dimensional_pass.cpp
// Tests: Dimensional analysis - correct unit operations
// From BLOG.md: Dimensional Analysis example

struct Quantity {
    int _dim_m;  // meters exponent
    int _dim_s;  // seconds exponent

    // Addition: dimensions must match exactly
    [[clang::annotate("dtt:requires=_dim_m == arg0._dim_m")]]
    [[clang::annotate("dtt:requires=_dim_s == arg0._dim_s")]]
    Quantity& operator+=(const Quantity& other) { return *this; }
};

// Variant 1: Add same dimensions (meters + meters)
void test_add_lengths() {
    Quantity len1;
    len1._dim_m = 1;
    len1._dim_s = 0;

    Quantity len2;
    len2._dim_m = 1;
    len2._dim_s = 0;

    len1 += len2;  // OK: same dimensions
}

// Variant 2: Add times (seconds + seconds)
void test_add_times() {
    Quantity t1;
    t1._dim_m = 0;
    t1._dim_s = 1;

    Quantity t2;
    t2._dim_m = 0;
    t2._dim_s = 1;

    t1 += t2;  // OK: same dimensions
}

// Variant 3: Add velocities (m/s + m/s)
void test_add_velocities() {
    Quantity v1;
    v1._dim_m = 1;
    v1._dim_s = -1;

    Quantity v2;
    v2._dim_m = 1;
    v2._dim_s = -1;

    v1 += v2;  // OK: same dimensions
}

int main() {
    test_add_lengths();
    test_add_times();
    test_add_velocities();
    return 0;
}
