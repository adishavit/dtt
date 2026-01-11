// WHAT IS TESTED:
// Expression constraint violation should fail.
//
// Expected: ERROR

[[clang::annotate("dtt:requires=arg0 + arg1 < 10")]]
void check_sum(int a, int b) {
    (void)a; (void)b;
}

void test_violation() {
    check_sum(5, 6);  // ERROR: 5 + 6 = 11 >= 10
}

int main() { test_violation(); return 0; }
