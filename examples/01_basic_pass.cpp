// Example 1: Basic usage — this should COMPILE
#include "dtt.h"

int main() {
    // Method 1: Annotate with known length
    DTT_LEN(3)
    Vec<int> v1;
    int arr1[] = {10, 20, 30};
    v1.data = arr1;
    v1.len = 3;
    
    int h = head(v1);  // OK: 3 > 0
    int s = second(v1); // OK: 3 > 1
    
    // Method 2: Assignment tracking
    Vec<int> v2;
    int arr2[] = {1, 2, 3, 4, 5};
    v2.data = arr2;
    v2.len = 5;  // Plugin tracks this
    
    int h2 = head(v2);  // OK: 5 > 0
    
    // Method 3: std::vector with initializer
    std::vector<int> v3 = {1, 2, 3};  // Plugin tracks length = 3
    int h3 = head(v3);  // OK: 3 > 0
    
    return h + s + h2 + h3;
}
