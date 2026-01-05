// Example 3: Runtime values with guards — this should COMPILE

#include <cstdio>

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len > 0")]]
int head(Vec& v) { return v.data[0]; }

[[clang::annotate("dtt:requires=len > 1")]]
int second(Vec& v) { return v.data[1]; }

int main(int argc, char**) {
    Vec v;
    int arr[] = {10, 20, 30, 40, 50};
    v.data = arr;
    v.len = argc;  // RUNTIME VALUE
    
    // With guard: compiles!
    if (v.len > 0) {
        int h = head(v);  // OK: guarded
        printf("head = %d\n", h);
    }
    
    // Nested guards
    if (v.len > 1) {
        int s = second(v);  // OK
        printf("second = %d\n", s);
    }
    
    return 0;
}
