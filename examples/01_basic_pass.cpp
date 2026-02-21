// Example 1: Basic usage — this should COMPILE

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len > 0")]]
int head(Vec& v) { return v.data[0]; }

[[clang::annotate("dtt:requires=len > 1")]]
int second(Vec& v) { return v.data[1]; }

int main() {
    // Method 1: Direct assignment
    int arr1[] = {10, 20, 30};
    Vec v1;
    v1.data = arr1;
    v1.len = 3;

    int h = head(v1);   // OK: 3 > 0
    int s = second(v1); // OK: 3 > 1

    // Method 2: Assignment tracking
    int arr2[] = {1, 2, 3, 4, 5};
    Vec v2;
    v2.data = arr2;
    v2.len = 5;  // Plugin tracks this

    int h2 = head(v2);  // OK: 5 > 0

    return h + s + h2;
}
