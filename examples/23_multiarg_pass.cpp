// Example 23: Multi-arg constraints — should COMPILE

struct Vec { int* data; int len; };

// Requires both vectors have same length
[[clang::annotate("dtt:requires=arg0.len == arg1.len")]]
int dot(Vec& a, Vec& b) {
    int sum = 0;
    for (int i = 0; i < a.len; i++)
        sum += a.data[i] * b.data[i];
    return sum;
}

int main() {
    int arr[5] = {1,2,3,4,5};
    Vec a, b;
    a.data = arr;
    a.len = 5;
    b.data = arr;
    b.len = 5;
    
    dot(a, b);  // OK: both have len = 5
    
    return 0;
}
