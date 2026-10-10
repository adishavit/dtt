// Example 115: Mixed const/non-const args — should FAIL
// Only non-const ref args are invalidated

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len >= 10")]]
void needsTen(Vec& v);

void mixedArgs(const Vec& src, Vec& dst);  // src is const, dst is not

void test() {
    Vec v1, v2;
    v1.len = 10;
    v2.len = 20;

    mixedArgs(v1, v2);  // v1 unchanged (const), v2 invalidated

    needsTen(v1);       // Should PASS: v1.len still = 10
    needsTen(v2);       // Should FAIL: v2.len is UNKNOWN  dtt-error {{len >= 10}}
}

int main() { test(); return 0; }
