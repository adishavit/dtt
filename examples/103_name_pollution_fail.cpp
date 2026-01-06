// Example 103: Proper failure detection — should FAIL
// Verify the fix doesn't break legitimate error detection

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len >= 10")]]
void needsTen(Vec& v);

void test() {
    Vec v;
    v.len = 5;  // Less than 10
    needsTen(v);  // Should FAIL: v.len = 5 < 10
}

int main() { test(); return 0; }
