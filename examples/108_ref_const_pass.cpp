// Example 108: Const reference doesn't invalidate — should COMPILE
// Const ref cannot modify the object

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len >= 10")]]
void needsTen(Vec& v);

void readOnly(const Vec& v);  // CONST reference

void test() {
    int arr[10] = {};
    Vec v;
    v.data = arr;
    v.len = 10;

    readOnly(v);    // Const ref - cannot modify v

    needsTen(v);    // Should PASS: v.len still = 10
}

int main() { test(); return 0; }
