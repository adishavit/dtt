// WHAT IS TESTED:
// Ternary expressions are not treated as guards for nullability.
//
// Expected behavior: PASS (ptr is non-null when chosen)
// Current behavior: FAIL (nullability unknown)

struct Data {
    int value;
};

void test(Data* ptr, Data* fallback, bool pick) {
    Data* chosen = pick ? ptr : fallback;
    chosen->value = 1;
}

int main() {
    Data d;
    test(&d, &d, true);
    return 0;
}
