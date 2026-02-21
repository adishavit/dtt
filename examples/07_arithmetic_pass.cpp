// Example 7: Type-level arithmetic (len = a + b)
struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len >= 5")]]
void needsFive(Vec& v) { (void)v; }

int main() {
    int arr[5] = {1,2,3,4,5};
    Vec a, b, c;
    a.data = arr;
    a.len = 3;
    b.data = arr;
    b.len = 2;

    // Plugin tracks: c.len = 3 + 2 = 5
    c.data = arr;
    c.len = a.len + b.len;
    
    needsFive(c);  // OK: 5 >= 5
    
    return 0;
}
