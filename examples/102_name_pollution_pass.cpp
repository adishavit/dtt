// Example 102: Cross-function variable names — should COMPILE
// Variables with same name in different functions should not collide

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len >= 10")]]
void needsTen(Vec& v);

// This function has a local "v" with len = 0
Vec makeEmpty() {
    Vec v;
    v.len = 0;
    return v;
}

// This function has a local "v" with len = 100
Vec makeFull() {
    Vec v;
    v.len = 100;
    return v;
}

void test() {
    Vec v;
    v.len = 50;

    // Even though makeEmpty() and makeFull() exist with their own "v",
    // THIS function's "v" has len = 50
    needsTen(v);  // Should PASS: v.len = 50
}

int main() { test(); return 0; }
