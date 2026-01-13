// Tests: Ternary expressions propagate nullability when branches are known non-null
// Issue #23: FIXED for literal non-null cases (address-of, new, string literals)

struct Data {
    int value;
};

// Variant 1: Both branches are address-of (always non-null)
void variant1_both_addressof(bool pick) {
    Data a, b;
    Data* chosen = pick ? &a : &b;
    chosen->value = 1;  // OK: both &a and &b are non-null
}

// Variant 2: Nested ternary with all non-null branches
void variant2_nested_ternary(bool a, bool b) {
    Data x, y, z;
    Data* chosen = a ? &x : (b ? &y : &z);
    chosen->value = 1;  // OK: all branches are address-of
}

// Variant 3: One branch is new (non-null), other is address-of
void variant3_new_and_addressof(bool pick) {
    Data d;
    Data* chosen = pick ? new Data() : &d;
    chosen->value = 1;  // OK: both new and &d are non-null
    if (pick) delete chosen;
}

// Variant 4: String literals in ternary
const char* variant4_string_literals(bool pick) {
    const char* s = pick ? "hello" : "world";
    return s;  // OK: both string literals are non-null
}

int main() {
    variant1_both_addressof(true);
    variant2_nested_ternary(true, false);
    variant3_new_and_addressof(false);
    variant4_string_literals(true);
    return 0;
}
