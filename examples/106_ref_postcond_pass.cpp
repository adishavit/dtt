// Example 106: Postcondition overrides invalidation — should COMPILE
// Function with postcondition restores tracking

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len >= 10")]]
void needsTen(Vec& v);

[[clang::annotate("dtt:ensures=result.len == 15")]]
void setFifteen(Vec& v);

void test() {
    Vec v;
    v.len = 10;

    setFifteen(v);  // Postcondition: v.len = 15

    needsTen(v);    // Should PASS: v.len = 15 >= 10
}

int main() { test(); return 0; }
