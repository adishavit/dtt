// Example 38: Using true branch of boolean — should COMPILE

struct Vec { int* data; int size; };

[[clang::annotate("dtt:ensures_if_true=arg0.size == 0")]]
[[clang::annotate("dtt:ensures_if_false=arg0.size > 0")]]
bool empty(Vec& v);

[[clang::annotate("dtt:requires=size == 0")]]
void handleEmpty(Vec& v);

int getValue();

int main() {
    Vec v;
    v.data = nullptr;
    v.size = getValue();  // initialized but unknown
    
    if (empty(v)) {
        // We know: v.size == 0 (from ensures_if_true)
        handleEmpty(v);  // OK!
    }
    
    return 0;
}
