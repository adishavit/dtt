// Example 21: Z3 chained arithmetic — should COMPILE

struct Vec { int* data; int len; };

[[clang::annotate("dtt:ensures=result.len == arg0.len - 1")]]
Vec tail(Vec& v);

[[clang::annotate("dtt:requires=len >= 3")]]
void needsThree(Vec& v);

int main() {
    int arr[10] = {};
    Vec a;
    a.data = arr;
    a.len = 10;
    
    Vec b = tail(a);   // b.len = 9
    Vec c = tail(b);   // c.len = 8
    Vec d = tail(c);   // d.len = 7
    Vec e = tail(d);   // e.len = 6
    Vec f = tail(e);   // f.len = 5
    Vec g = tail(f);   // g.len = 4
    
    // Z3 proves: 4 >= 3
    needsThree(g);
    
    return 0;
}
