// A forwarding reference bound to an lvalue is a mutable reference.
// The unknown callee can change size even without moving the object.

struct Vec {
    int* data;
    int _size;
};

// Forwarding reference: a mutable lvalue may be changed by this callee.
template<typename T>
void maybe_consume(T&& v);

[[clang::annotate("dtt:requires=_size > 0")]]
void process(Vec& v);

void test() {
    Vec v;
    v._size = 10;

    maybe_consume(v);  // Passing LVALUE - T&& deduces to Vec&, not Vec&&

    // The call leaves the tracked property unknown.
    process(v);  // ERROR: a mutable lvalue may have been changed  dtt-error {{_size > 0}}
}

int main() { test(); return 0; }
