// Example 33: old() for relative postconditions — should COMPILE

struct Vec { int* data; int size; };

[[clang::annotate("dtt:ensures=result.size == old(arg0.size) + 1")]]
void push_back(Vec& v, int x);

[[clang::annotate("dtt:requires=size >= 6")]]
void needsSix(Vec& v);

int main() {
    Vec v;
    v.data = nullptr;
    v.size = 5;
    
    push_back(v, 42);  // v.size = old(5) + 1 = 6
    
    needsSix(v);  // OK: v.size = 6 >= 6
    
    return 0;
}
