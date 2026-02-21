// Example 114: Const pointer doesn't invalidate — should COMPILE
// Pointer to const cannot modify the pointee

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len >= 10")]]
void needsTen(Vec& v);

void readOnlyPtr(const Vec* v);  // Const pointer

void test() {
    int arr[10] = {};
    Vec v;
    v.data = arr;
    v.len = 10;

    readOnlyPtr(&v);  // Const pointer - cannot modify v

    needsTen(v);      // Should PASS: v.len still = 10
}

int main() { test(); return 0; }
