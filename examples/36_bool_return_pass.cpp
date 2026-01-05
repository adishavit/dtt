// Example 36: Boolean return postconditions — should COMPILE

struct Vec { int* data; int size; };

[[clang::annotate("dtt:ensures_if_true=arg0.size == 0")]]
[[clang::annotate("dtt:ensures_if_false=arg0.size > 0")]]
bool empty(Vec& v);

[[clang::annotate("dtt:requires=size > 0")]]
int head(Vec& v);

int main() {
    Vec v;
    v.size = 5;
    
    if (!empty(v)) {
        // We know: v.size > 0 (from ensures_if_false)
        head(v);  // OK!
    }
    
    return 0;
}
