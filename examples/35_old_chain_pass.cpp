// Example 35: Chained old() calls — should COMPILE

struct Vec { int* data; int size; };

[[clang::annotate("dtt:ensures=result.size == old(arg0.size) + 1")]]
void push_back(Vec& v, int x);

[[clang::annotate("dtt:requires=size >= 8")]]
void needsEight(Vec& v);

int main() {
    Vec v;
    v.size = 5;
    
    push_back(v, 1);  // v.size = 6
    push_back(v, 2);  // v.size = 7
    push_back(v, 3);  // v.size = 8
    
    needsEight(v);  // OK: 5 + 3 = 8
    
    return 0;
}
