// Example 111: Method call invalidates object — should FAIL
// Non-const method on tracked object invalidates it

struct Vec {
    int* data;
    int len;

    void mutate();  // Non-const method, no postcondition
};

[[clang::annotate("dtt:requires=len >= 10")]]
void needsTen(Vec& v);

void test() {
    Vec v;
    v.len = 10;

    v.mutate();     // Non-const method - may modify v, invalidate

    needsTen(v);    // Should FAIL: v.len is UNKNOWN
}

int main() { test(); return 0; }
