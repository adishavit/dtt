// Example 84: Z3 subtraction insufficient — should FAIL
// Z3 correctly rejects: if len >= 5, then len - 3 is only >= 2

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len >= 10")]]
void needsTen(Vec& v);

int getValue();

int main() {
    Vec a;
    a.len = getValue();
    
    Vec b;
    b.len = a.len - 3;  // Symbolic: a.len - 3
    
    if (a.len >= 5) {
        // Z3 finds counterexample: a.len = 5 => b.len = 2 < 10
        needsTen(b);  // ERROR: insufficient proof
    }
    
    return 0;
}
