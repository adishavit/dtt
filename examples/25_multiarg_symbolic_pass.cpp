// Example 25: Multi-arg with same constant — should COMPILE

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=arg0.len == arg1.len")]]
int dot(Vec& a, Vec& b);

int main() {
    int arr[5] = {1,2,3,4,5};
    Vec a, b;
    a.data = arr;
    a.len = 5;
    b.data = arr;
    b.len = 5;  // Same constant
    
    dot(a, b);  // OK: both have len = 5
    
    return 0;
}
