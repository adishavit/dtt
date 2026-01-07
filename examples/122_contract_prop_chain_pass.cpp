// Example 122: Contract propagation — chained function calls — should COMPILE
// A stronger precondition implies a weaker one

struct Vec { int* data; int _size; };

[[clang::annotate("dtt:requires=_size > 0")]]
int pop(Vec& v);

[[clang::annotate("dtt:requires=_size >= 1")]]
int first(Vec& v);

// This function requires _size >= 3 and calls pop() which requires _size > 0
// _size >= 3 implies _size > 0, so this should work
[[clang::annotate("dtt:requires=_size >= 3")]]
int processPop(Vec& v) {
    return pop(v);  // Should PASS: _size >= 3 implies _size > 0
}

// This function requires _size >= 10 and calls first() which requires _size >= 1
// _size >= 10 implies _size >= 1, so this should work
[[clang::annotate("dtt:requires=_size >= 10")]]
int processFirst(Vec& v) {
    return first(v);  // Should PASS: _size >= 10 implies _size >= 1
}

int main() { return 0; }
