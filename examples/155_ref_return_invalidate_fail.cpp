// Example 155: Function returning reference should invalidate — should FAIL
//
// Functions returning T& often modify their argument and return a
// reference to part of it (e.g., T& Vector::push_back_and_get(T&&)).
// The modifiable ref parameter should trigger invalidation.

struct Vec {
    int* data;
    int _size;
};

// Returns reference, takes modifiable ref - should invalidate
int& get_and_maybe_resize(Vec& v);

[[clang::annotate("dtt:requires=_size > 0")]]
void process(Vec& v);

void test() {
    Vec v;
    v._size = 10;

    int& ref = get_and_maybe_resize(v);  // Might resize, invalidates v._size

    process(v);  // Should FAIL: v._size unknown after get_and_maybe_resize
}

int main() { test(); return 0; }
