// Example 110: Pointer argument invalidates — should FAIL
// Pointer can modify the pointee

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len >= 10")]]
void needsTen(Vec& v);

void modify(Vec* v);  // Pointer argument, no postcondition

void test() {
    Vec v;
    v.len = 10;

    modify(&v);     // Pointer - can modify v, invalidate

    needsTen(v);    // Should FAIL: v.len is UNKNOWN
}

int main() { test(); return 0; }
