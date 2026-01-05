// Example 19: Z3 arithmetic disproof — should FAIL

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len >= 20")]]
void needsTwenty(Vec& v);

int main() {
    Vec a, b, c;
    a.len = 7;
    b.len = 5;
    c.len = a.len + b.len;  // c.len = 12
    
    // Z3 disproves: 12 >= 20 is false
    needsTwenty(c);
    
    return 0;
}
