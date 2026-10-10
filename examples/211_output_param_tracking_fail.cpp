// An external output function with no contract or visible body provides no
// proof of the output size. Enforce mode must report the later requirement.

struct Vec {
    int* data;
    int _size;
};

// No output annotation is present here.
void create_vec(Vec& out, int count);

[[clang::annotate("dtt:requires=_size > 0")]]
void process(Vec& v);

void test() {
    Vec v;
    // v._size is unknown after the external call.

    create_vec(v, 10);  // No postcondition is declared.

    // The call leaves the tracked property unknown.
    process(v);  // ERROR: create_vec declares no output guarantee  dtt-error {{_size > 0}}
}

int main() { test(); return 0; }
