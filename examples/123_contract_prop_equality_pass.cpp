// Example 123: Contract propagation — equality constraint — should COMPILE
// Exact value constraint becomes usable inside function body

struct Buffer { int* data; int _size; };

[[clang::annotate("dtt:requires=_size == 10")]]
void needsExactlyTen(Buffer& b);

[[clang::annotate("dtt:requires=_size >= 5")]]
void needsAtLeastFive(Buffer& b);

// With _size == 10, we can satisfy both _size == 10 and _size >= 5
[[clang::annotate("dtt:requires=_size == 10")]]
void processTenItems(Buffer& b) {
    needsExactlyTen(b);   // Should PASS: _size == 10
    needsAtLeastFive(b);  // Should PASS: _size == 10 >= 5
}

int main() { return 0; }
