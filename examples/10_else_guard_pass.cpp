// Example 10: Else-branch guards — should COMPILE

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len > 0")]]
int head(Vec& v);

[[clang::annotate("dtt:requires=len <= 0")]]
void handleEmpty(Vec& v);

int main(int argc, char**) {
    Vec v;
    v.len = argc;  // Runtime value
    
    if (v.len > 0) {
        head(v);       // OK: guarded by len > 0
    } else {
        handleEmpty(v); // OK: else means len <= 0
    }
    
    return 0;
}
