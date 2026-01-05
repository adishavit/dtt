// Example 13: Chained postconditions — should FAIL

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len > 0")]]
[[clang::annotate("dtt:ensures=result.len == arg0.len - 1")]]
Vec tail(Vec& v);

[[clang::annotate("dtt:requires=len >= 2")]]
int secondElement(Vec& v) { return v.data[1]; }

int main() {
    Vec v;
    v.len = 3;

    Vec a = tail(v);     // a.len = 3 - 1 = 2
    Vec b = tail(a);     // b.len = 2 - 1 = 1

    secondElement(b);    // ERROR: 1 >= 2 is false

    return 0;
}
