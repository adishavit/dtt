// Example 85: Nested struct with guards — should COMPILE
// Tests guard propagation through nested struct access

struct Inner { int len; };
struct Outer { Inner inner; int* data; };

[[clang::annotate("dtt:requires=len > 0")]]
int getFirst(Inner& i);

int main() {
    Outer o;
    o.inner.len = 5;
    
    if (o.inner.len > 0) {
        getFirst(o.inner);  // OK: guard proves len > 0
    }
    
    return 0;
}
