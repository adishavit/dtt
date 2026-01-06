// Example 24: TRULY requires Z3 — should FAIL
// Z3 correctly rejects insufficient proof

struct Vec { int* data; int len; };

int getValue();  // Unknown runtime value

[[clang::annotate("dtt:requires=len >= 10")]]
void needsTen(Vec& v);

int main() {
    Vec a;
    a.len = getValue();  // Unknown

    Vec b;
    b.len = a.len + 5;   // Symbolic: a.len + 5

    if (a.len >= 3) {
        // Z3 determines this is UNPROVABLE:
        //   Given: (a.len >= 3) AND (b.len = a.len + 5)
        //   Prove: b.len >= 10
        //   Minimum b.len = 3 + 5 = 8, but need 10
        //   COUNTEREXAMPLE: a.len = 3 → b.len = 8 < 10
        needsTen(b);  // ERROR: Z3 finds counterexample
    }

    return 0;
}
