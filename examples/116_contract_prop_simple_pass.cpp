// Example 116: Contract propagation — simple constraint — should COMPILE
// dtt:requires= becomes an assumption inside the function body

struct Vec { int* data; int _size; };

[[clang::annotate("dtt:requires=_size > 0")]]
int front(Vec& v);

// This function has requires=_size > 0, so inside the body
// we can safely call front() which also requires _size > 0
[[clang::annotate("dtt:requires=_size > 0")]]
int getFirst(Vec& v) {
    return front(v);  // Should PASS: we have proof that v._size > 0
}

int main() { return 0; }
