// Example 120: Contract propagation — compound AND constraint — should COMPILE
// Both parts of AND are available as assumptions

struct Vec { int* data; int _size; int _capacity; };

[[clang::annotate("dtt:requires=_size > 0")]]
int front(Vec& v);

[[clang::annotate("dtt:requires=_capacity >= 10")]]
void reserve(Vec& v);

// Compound constraint: both _size > 0 AND _capacity >= 10
[[clang::annotate("dtt:requires=_size > 0 && _capacity >= 10")]]
void processWithCapacity(Vec& v) {
    front(v);     // Should PASS: we have _size > 0
    reserve(v);   // Should PASS: we have _capacity >= 10
}

int main() { return 0; }
