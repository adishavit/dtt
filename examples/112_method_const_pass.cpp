// Example 112: Const method doesn't invalidate — should COMPILE
// Const method cannot modify the object

struct Vec {
    int* data;
    int len;

    int getLen() const;  // CONST method
};

[[clang::annotate("dtt:requires=len >= 10")]]
void needsTen(Vec& v);

void test() {
    int arr[10] = {};
    Vec v;
    v.data = arr;
    v.len = 10;

    v.getLen();     // Const method - cannot modify v

    needsTen(v);    // Should PASS: v.len still = 10
}

int main() { test(); return 0; }
