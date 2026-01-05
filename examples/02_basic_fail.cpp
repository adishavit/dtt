// Example 2: Type errors — this should NOT compile
#include "dtt.h"

int main() {
    // Error 1: Empty vector
    DTT_LEN(0)
    Vec<int> empty;
    head(empty);  // ERROR: 0 > 0 is false
    
    // Error 2: Length too small for second()
    DTT_LEN(1)
    Vec<int> single;
    second(single);  // ERROR: 1 > 1 is false
    
    // Error 3: Unknown length (no annotation, no tracking)
    Vec<int> unknown;
    head(unknown);  // ERROR: unknown.len is unknown
    
    return 0;
}
