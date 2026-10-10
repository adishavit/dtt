// Example 153: Combined soundness test — should FAIL
//
// Tests two soundness improvements:
// 1. Rvalue references (T&&) now trigger property invalidation
// 2. Non-void functions with modifiable ref params now invalidate
//
// Note: Annotated-but-no-ensures methods are trusted by design (see 151_xfail).

#include <utility>  // std::move

struct Container {
    int* data;
    int _size;
};

// Takes rvalue ref - should invalidate (no DTT annotations)
void sink(Container&& c);

// Returns value but mutates ref - should invalidate (no DTT annotations)
int extract(Container& c);

[[clang::annotate("dtt:requires=_size > 0")]]
void process(Container& c);

void test_rvalue_ref() {
    Container c;
    c._size = 5;
    sink(std::move(c));
    process(c);  // Should FAIL: c._size unknown after move  dtt-error {{arg0\._size > 0}}
}

void test_nonvoid_mutation() {
    Container c;
    c._size = 5;
    int val = extract(c);
    process(c);  // Should FAIL: c._size unknown after extract()  dtt-error {{arg0\._size > 0}}
}

int main() {
    test_rvalue_ref();
    test_nonvoid_mutation();
    return 0;
}
