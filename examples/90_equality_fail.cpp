// Example 90: Equality constraint violation — should FAIL
// Tests == constraint with wrong value

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len == 5")]]
void needsFive(Vec& v);

int main() {
    Vec v;
    v.len = 7;
    needsFive(v);  // ERROR: len = 7, not 5  dtt-error {{len == 5}}
    
    return 0;
}
