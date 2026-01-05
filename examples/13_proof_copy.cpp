// Example 13: Proof copies with assignment — should COMPILE

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len > 0")]]
int head(Vec& v);

int main(int argc, char**) {
    Vec v;
    v.len = argc;
    
    if (v.len > 0) { }  // v has proof
    
    Vec w = v;   // w copies v's proof
    head(w);     // OK: w has proof
    
    return 0;
}
