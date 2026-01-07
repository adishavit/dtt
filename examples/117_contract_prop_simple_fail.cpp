// Example 117: Contract propagation — insufficient constraint — should FAIL
// The function requires more than our precondition provides

struct Vec { int* data; int _size; };

[[clang::annotate("dtt:requires=_size >= 10")]]
void needsTen(Vec& v);

// This function only requires _size > 0, which is weaker than >= 10
[[clang::annotate("dtt:requires=_size > 0")]]
void process(Vec& v) {
    needsTen(v);  // Should FAIL: _size > 0 does NOT imply _size >= 10
}

int main() { return 0; }
