// Example 101: Lambda timing — should COMPILE
// Lambda body should not affect outer scope until called

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len > 0")]]
void needsNonEmpty(Vec& v);

void test() {
    Vec v;
    v.len = 10;  // v.len = 10

    // Lambda DEFINED but NOT CALLED
    auto lambda = [&v]() {
        v.len = 0;  // This should NOT affect outer scope yet
    };

    // v.len should still be 10 here
    needsNonEmpty(v);  // Should PASS: v.len = 10

    // Lambda never invoked in this test
}

int main() { test(); return 0; }
