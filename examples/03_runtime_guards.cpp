// Example 3: Runtime values with guards — this should COMPILE
#include "dtt.h"
#include <cstdio>

int main(int argc, char**) {
    // Runtime value from command line
    Vec<int> v;
    int arr[] = {10, 20, 30, 40, 50};
    v.data = arr;
    v.len = argc;  // RUNTIME VALUE — unknown at compile time
    
    // Without guard: would fail
    // head(v);  // ERROR: v.len is unknown
    
    // With guard: compiles!
    if (v.len > 0) {
        // Inside this block, plugin knows v.len > 0
        int h = head(v);  // OK: guarded
        printf("head = %d\n", h);
    } else {
        printf("empty vector\n");
    }
    
    // Nested guards work too
    if (v.len > 1) {
        int s = second(v);  // OK: v.len > 1 implies v.len > 0
        printf("second = %d\n", s);
    }
    
    return 0;
}
