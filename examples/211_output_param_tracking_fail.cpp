// Output parameter tracking — should COMPILE but currently FAILS
//
// WHAT IS TESTED:
// Functions with annotated output parameters should set properties
// on the passed argument, not just check preconditions.
//
// WHY IT'S DIFFICULT:
// 1. Need to distinguish input params (checked) from output params (set)
// 2. Annotation syntax would need to specify which arg gets the property
// 3. Multiple output params complicate the annotation model
// 4. Partial success (some outputs set, others not) is hard to model
//
// RATIONALE:
// Output parameters are common in C++ APIs:
// - bool TryParse(const string& in, int& out)
// - void GetSize(Container& c, int& width, int& height)
// - Error Read(Buffer& buf, size_t& bytes_read)
//
// CURRENT BEHAVIOR:
// DTT invalidates non-const ref params for unannotated functions.
// There's no way to specify that a function SETS a property on an argument.

struct Vec {
    int* data;
    int _size;
};

// Hypothetical annotation: sets out._size to the count
// [[clang::annotate("dtt:ensures=arg0._size == count")]]  // NOT SUPPORTED
void create_vec(Vec& out, int count);

[[clang::annotate("dtt:requires=_size > 0")]]
void process(Vec& v);

void test() {
    Vec v;
    // v._size is uninitialized/unknown

    create_vec(v, 10);  // Should set v._size = 10

    // XFAIL: v._size is unknown (invalidated, not tracked)
    process(v);  // Should PASS if output param tracking worked
}

int main() { test(); return 0; }
