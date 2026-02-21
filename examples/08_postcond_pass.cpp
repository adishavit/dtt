// Example 8: Postconditions — should COMPILE

struct Vec { int* data; int len; };

// tail: removes first element, result.len = arg0.len - 1
[[clang::annotate("dtt:requires=len > 0")]]
[[clang::annotate("dtt:ensures=result.len == arg0.len - 1")]]
Vec tail(Vec& v);

// requiresFour: needs len >= 4
[[clang::annotate("dtt:requires=len >= 4")]]
void requiresFour(Vec& v);

// requiresThree: needs len >= 3
[[clang::annotate("dtt:requires=len >= 3")]]
void requiresThree(Vec& v);

int main() {
    int arr[5] = {1,2,3,4,5};
    Vec a;
    a.data = arr;
    a.len = 5;
    
    Vec b = tail(a);  // b.len = 5 - 1 = 4
    requiresFour(b);  // OK: 4 >= 4
    
    Vec c = tail(b);  // c.len = 4 - 1 = 3
    requiresThree(c); // OK: 3 >= 3
    
    return 0;
}
