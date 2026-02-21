// Example 20: Z3 with symbolic values and proofs — should COMPILE

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len >= 5")]]
void needsFive(Vec& v);

int getValue();  // Unknown runtime value

int main() {
    int arr[1] = {0};
    Vec v;
    v.data = arr;
    v.len = getValue();  // Unknown
    
    // Guard establishes: v.len > 10
    if (v.len > 10) {
        // Z3 proves: (v.len > 10) implies (v.len >= 5)
        needsFive(v);
    }
    
    return 0;
}
