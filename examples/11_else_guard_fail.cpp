// Example 11: Else-branch wrong usage — should FAIL

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len > 0")]]
int head(Vec& v);

int main(int argc, char**) {
    Vec v;
    v.len = argc;  // Runtime value
    
    if (v.len > 0) {
        // OK here
    } else {
        head(v);  // ERROR: in else branch, len <= 0, so len > 0 is false  dtt-error {{len > 0}}
    }
    
    return 0;
}
