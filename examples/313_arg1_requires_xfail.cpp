// WHAT IS TESTED:
// Requires constraints on arg1 are mis-evaluated (checked against arg0).
//
// Expected behavior: PASS (arg1._size = 2 satisfies constraint)
// Current behavior: FAIL (constraint checked against arg0 path)

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
    a._size = 1;
    b._size = 2;
    use(a, b);
}

int main() { test(); return 0; }
