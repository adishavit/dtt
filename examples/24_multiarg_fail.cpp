// Example 24: Multi-arg constraint violation — should FAIL

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=arg0.len == arg1.len")]]
int dot(Vec& a, Vec& b);

int main() {
    Vec a, b;
    a.len = 5;
    b.len = 3;  // Different!
    
    dot(a, b);  // ERROR: 5 != 3
    
    return 0;
}
