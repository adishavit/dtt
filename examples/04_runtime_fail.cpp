// Example 4: Runtime value WITHOUT guard — should NOT compile
#include "dtt.h"

int main(int argc, char**) {
    Vec<int> v;
    int arr[] = {1, 2, 3};
    v.data = arr;
    v.len = argc;  // Runtime value
    
    // No guard — plugin cannot verify len > 0
    return head(v);  // ERROR: v.len is unknown
}
