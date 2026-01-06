// Example 113: Annotated function without ensures — should COMPILE
// Functions with dtt:requires but no dtt:ensures should NOT invalidate

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len >= 10")]]
void needsTen(Vec& v);

// This function has requires but no ensures - should NOT invalidate v
[[clang::annotate("dtt:requires=len > 0")]]
void checkPositive(Vec& v);

void test() {
    Vec v;
    v.len = 20;

    checkPositive(v);  // Has requires, no ensures - should NOT invalidate

    needsTen(v);       // Should PASS: v.len still = 20
}

int main() { test(); return 0; }
