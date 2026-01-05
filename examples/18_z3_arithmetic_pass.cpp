// Example 18: Z3 arithmetic proof — should COMPILE
// This case CANNOT be solved without Z3

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len > 0")]]
int head(Vec& v);

[[clang::annotate("dtt:requires=len >= 10")]]
void needsTen(Vec& v);

int main() {
    Vec a, b, c;
    a.len = 7;
    b.len = 5;
    c.len = a.len + b.len;  // c.len = 7 + 5 = 12
    
    // Z3 proves: 12 >= 10
    needsTen(c);
    
    // Z3 proves: 12 > 0
    head(c);
    
    return 0;
}
