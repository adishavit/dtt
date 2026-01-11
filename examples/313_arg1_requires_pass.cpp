// WHAT IS TESTED:
// Requires constraints on argN should be checked against the (N+1)th argument.
// arg1._size > 0 should verify the second argument's _size, not arg0's.
//
// Expected: PASS (all constraints satisfied)

struct Vec {
    int _size;
};

[[clang::annotate("dtt:requires=arg1._size > 0")]]
void use2(Vec& a, Vec& b) {
    (void)a;
    (void)b;
}

[[clang::annotate("dtt:requires=arg2._size > 0")]]
void use3(Vec& a, Vec& b, Vec& c) {
    (void)a;
    (void)b;
    (void)c;
}

void test_arg1_basic() {
    Vec a, b;
    a._size = 1;
    b._size = 2;  // arg1._size = 2 > 0, satisfies constraint
    use2(a, b);
}

void test_arg1_arg0_irrelevant() {
    Vec a, b;
    a._size = 0;  // arg0._size = 0 (doesn't matter for arg1 constraint)
    b._size = 2;  // arg1._size = 2 > 0, satisfies constraint
    use2(a, b);
}

void test_arg2_constraint() {
    Vec a, b, c;
    a._size = 0;
    b._size = 0;
    c._size = 3;  // arg2._size = 3 > 0, satisfies constraint
    use3(a, b, c);
}

int main() {
    test_arg1_basic();
    test_arg1_arg0_irrelevant();
    test_arg2_constraint();
    return 0;
}
