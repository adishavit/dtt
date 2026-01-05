// Example 12: Proof persists beyond scope — should COMPILE

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len > 0")]]
int head(Vec& v);

int main(int argc, char**) {
    Vec v;
    v.len = argc;  // Runtime value
    
    if (v.len > 0) {
        // Proof established
    }
    
    // Proof persists after the if!
    head(v);  // OK
    
    return 0;
}
