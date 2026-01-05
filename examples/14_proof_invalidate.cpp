// Example 14: Mutation invalidates proof — should FAIL

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len > 0")]]
int head(Vec& v);

int getVal();

int main() {
    Vec v;
    v.len = 5;
    
    if (v.len > 0) { }  // Proof established
    
    head(v);           // OK: proof exists
    
    v.len = getVal();  // Invalidates proof AND sets to unknown
    
    head(v);           // FAIL: no proof, unknown value
    
    return 0;
}
