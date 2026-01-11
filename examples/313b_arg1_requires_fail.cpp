// WHAT IS TESTED:
// Requires constraints on arg1 should be checked against the second argument.
// arg1._size > 0 should verify b._size, not a._size.
//
// Expected: ERROR (b._size = 0 violates arg1._size > 0)

struct Vec {
    int _size;
};

[[clang::annotate("dtt:requires=arg1._size > 0")]]
void use(Vec& a, Vec& b) {
    (void)a;
    (void)b;
}

void test() {
    Vec a;
    Vec b;
    a._size = 5;  // arg0._size = 5 (doesn't matter for this constraint)
    b._size = 0;  // arg1._size = 0, violates > 0
    use(a, b);
}

int main() { test(); return 0; }
