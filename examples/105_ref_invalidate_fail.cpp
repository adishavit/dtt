// Example 105: Use after invalidation — should FAIL
// Cannot prove constraint after passing to unknown function

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len >= 10")]]
void needsTen(Vec& v);

void unknown(Vec& v);  // No postcondition

void test() {
    Vec v;
    v.len = 10;

    unknown(v);     // Invalidates v.len → UNKNOWN

    needsTen(v);    // Should FAIL: v.len is UNKNOWN, cannot prove >= 10  dtt-error {{len >= 10}}
}

int main() { test(); return 0; }
