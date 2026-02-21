// Example 107: Pass by value doesn't invalidate — should COMPILE
// Copy semantics - original is unchanged

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len >= 10")]]
void needsTen(Vec& v);

void takeByValue(Vec v);  // By VALUE, not reference

void test() {
    Vec v;
    v.data = nullptr;
    v.len = 10;

    takeByValue(v);  // Pass by VALUE - v is copied, original unchanged

    needsTen(v);     // Should PASS: v.len still = 10
}

int main() { test(); return 0; }
