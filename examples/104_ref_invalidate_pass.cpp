// Example 104: Re-assignment after invalidation — should COMPILE
// After passing by ref to unknown function, re-assignment restores tracking

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len >= 10")]]
void needsTen(Vec& v);

void unknown(Vec& v);  // No postcondition

void test() {
    Vec v;
    v.data = nullptr;
    v.len = 10;

    unknown(v);     // Invalidates v.len → UNKNOWN

    v.len = 20;     // Re-assignment → v.len = 20 (tracked again)

    needsTen(v);    // Should PASS: v.len = 20 >= 10
}

int main() { test(); return 0; }
