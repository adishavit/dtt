// Example 25: Multi-arg with same constant — should COMPILE

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=arg0.len == arg1.len")]]
int dot(Vec& a, Vec& b);

int main() {
    Vec a, b;
    a.len = 5;
    b.len = 5;  // Same constant
    
    dot(a, b);  // OK: both have len = 5
    
    return 0;
}
