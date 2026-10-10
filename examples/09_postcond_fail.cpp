// Example 9: Postconditions — should FAIL

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len > 0")]]
[[clang::annotate("dtt:ensures=result.len == arg0.len - 1")]]
Vec tail(Vec& v);

[[clang::annotate("dtt:requires=len >= 4")]]
void requiresFour(Vec& v);

int main() {
    Vec a;
    a.len = 5;
    
    Vec b = tail(a);  // b.len = 5 - 1 = 4
    Vec c = tail(b);  // c.len = 4 - 1 = 3
    
    requiresFour(c);  // ERROR: 3 >= 4 is false  dtt-error {{len >= 4}}
    
    return 0;
}
