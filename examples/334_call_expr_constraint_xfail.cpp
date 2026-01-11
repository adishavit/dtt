// WHAT IS TESTED:
// Expression constraints on free functions don't substitute arg values.
//
// Expected behavior: PASS (1 + 2 < 10)
// Current behavior: FAIL (args not substituted in expr constraint)

[[clang::annotate("dtt:requires=arg0 + arg1 < 10")]]
void check(int a, int b) {
    (void)a;
    (void)b;
}

void test() {
    check(1, 2);
}

int main() { test(); return 0; }
