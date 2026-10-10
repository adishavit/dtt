// Example 37: Boolean return without check — should FAIL

struct Vec { int* data; int size; };

[[clang::annotate("dtt:ensures_if_true=arg0.size == 0")]]
[[clang::annotate("dtt:ensures_if_false=arg0.size > 0")]]
bool empty(Vec& v);

[[clang::annotate("dtt:requires=size > 0")]]
int head(Vec& v);

int main() {
    Vec v;
    // v.size is unknown
    
    head(v);  // ERROR: v.size is unknown, no guard  dtt-error {{size > 0}}
    
    return 0;
}
