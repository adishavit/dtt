// Example 100: Z3 chain reasoning — should COMPILE
// Tests: Z3 proves transitive properties through chain

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len >= 20")]]
void needsTwenty(Vec& v);

int getValue();

int main() {
    Vec a;
    a.len = getValue();
    
    Vec b;
    b.len = a.len + 5;  // b.len = a.len + 5
    
    Vec c;
    c.len = b.len + 10;  // c.len = b.len + 10 = a.len + 15
    
    if (a.len >= 5) {
        // Z3 proves:
        //   Given: a.len >= 5, b.len = a.len + 5, c.len = b.len + 10
        //   Therefore: c.len = a.len + 15 >= 5 + 15 = 20
        needsTwenty(c);  // OK: Z3 proves c.len >= 20
    }
    
    return 0;
}
