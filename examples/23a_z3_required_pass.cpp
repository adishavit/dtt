// Example 23: TRULY requires Z3 — should COMPILE
// Simple checker CANNOT solve this - needs SMT reasoning

struct Vec { int* data; int len; };

int getValue();  // Unknown runtime value

[[clang::annotate("dtt:requires=len >= 8")]]
void needsEight(Vec& v);

[[clang::annotate("dtt:requires=len >= 15")]]
void needsFifteen(Vec& v);

int main() {
    int arr[1] = {0};
    Vec a;
    a.data = arr;
    a.len = getValue();  // Unknown - can't evaluate

    Vec b;
    b.data = arr;
    b.len = a.len + 5;   // Symbolic: a.len + 5, NOT evaluable

    if (a.len >= 3) {
        // Simple checker sees:
        //   - b.len = symbolic (eval returns nullopt)
        //   - No proof directly on b
        //   - FAILS
        //
        // Z3 proves:
        //   Given: (a.len >= 3) AND (b.len = a.len + 5)
        //   Prove: b.len >= 8
        //   3 + 5 = 8, so minimum b.len = 8 ✓
        needsEight(b);
    }

    if (a.len >= 10) {
        // Z3 proves: 10 + 5 = 15, so b.len >= 15
        needsFifteen(b);
    }

    return 0;
}
