// Example 86: Multiple guards on same object — should COMPILE
// Tests that multiple proof facts combine correctly

struct Vec { int* data; int len; int cap; };

[[clang::annotate("dtt:requires=len > 0")]]
[[clang::annotate("dtt:requires=cap > 0")]]
void process(Vec& v);

int main() {
    Vec v;
    v.len = 5;
    v.cap = 10;
    
    if (v.len > 0 && v.cap > 0) {
        process(v);  // OK: both constraints satisfied
    }
    
    return 0;
}
