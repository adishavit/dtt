// Example 109: Multiple ref args all invalidated — should FAIL
// All non-const ref arguments get invalidated

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len >= 10")]]
void needsTen(Vec& v);

void swapVecs(Vec& a, Vec& b);  // No postcondition, both invalidated

void test() {
    Vec v1, v2;
    v1.len = 10;
    v2.len = 20;

    swapVecs(v1, v2);   // Both v1 and v2 invalidated

    needsTen(v1);   // Should FAIL: v1.len is UNKNOWN
}

int main() { test(); return 0; }
