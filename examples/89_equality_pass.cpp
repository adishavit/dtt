// Example 89: Equality constraint — should COMPILE
// Tests == constraint checking

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len == 5")]]
void needsFive(Vec& v);

int main() {
    Vec v;
    v.len = 5;
    needsFive(v);  // OK: len == 5
    
    return 0;
}
