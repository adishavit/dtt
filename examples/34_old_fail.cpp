// Example 34: old() postcondition not sufficient — should FAIL

struct Vec { int* data; int size; };

[[clang::annotate("dtt:ensures=result.size == old(arg0.size) + 1")]]
void push_back(Vec& v, int x);

[[clang::annotate("dtt:requires=size >= 7")]]
void needsSeven(Vec& v);

int main() {
    Vec v;
    v.size = 5;
    
    push_back(v, 42);  // v.size = old(5) + 1 = 6
    
    needsSeven(v);  // ERROR: v.size = 6, needs >= 7
    
    return 0;
}
