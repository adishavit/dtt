// Example 83: Z3 multiplication reasoning — should COMPILE
// Z3 proves: if len >= 5, then len * 2 >= 10

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len >= 10")]]
void needsTen(Vec& v);

int getValue();

int main() {
    Vec a;
    a.len = getValue();
    
    Vec b;
    b.len = a.len * 2;  // Symbolic: a.len * 2
    
    if (a.len >= 5) {
        // Z3 proves: a.len >= 5 AND b.len = a.len * 2
        //           => b.len >= 10
        needsTen(b);  // OK: Z3 proves this
    }
    
    return 0;
}
