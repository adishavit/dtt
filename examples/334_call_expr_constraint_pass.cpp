// WHAT IS TESTED:
// Expression constraints with arg substitution should work.
//
// Expected: PASS

[[clang::annotate("dtt:requires=arg0 + arg1 < 10")]]
void check_sum(int a, int b) {
    (void)a; (void)b;
}

[[clang::annotate("dtt:requires=arg0 * arg1 < 100")]]
void check_product(int a, int b) {
    (void)a; (void)b;
}

[[clang::annotate("dtt:requires=arg0 - arg1 > 0")]]
void check_diff(int a, int b) {
    (void)a; (void)b;
}

// 1. Sum constraint
void test_sum() {
    check_sum(1, 2);  // 1 + 2 = 3 < 10
    check_sum(4, 5);  // 4 + 5 = 9 < 10
}

// 2. Product constraint
void test_product() {
    check_product(3, 3);  // 3 * 3 = 9 < 100
    check_product(9, 9);  // 9 * 9 = 81 < 100
}

// 3. Difference constraint
void test_diff() {
    check_diff(5, 2);  // 5 - 2 = 3 > 0
    check_diff(10, 1); // 10 - 1 = 9 > 0
}

// 4. Multiple args in expression
[[clang::annotate("dtt:requires=arg0 + arg1 + arg2 < 15")]]
void check_triple(int a, int b, int c) {
    (void)a; (void)b; (void)c;
}

void test_triple() {
    check_triple(1, 2, 3);  // 1 + 2 + 3 = 6 < 15
}

int main() {
    test_sum();
    test_product();
    test_diff();
    test_triple();
    return 0;
}
