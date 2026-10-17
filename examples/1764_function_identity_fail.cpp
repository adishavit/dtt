// 1764: A function's contract belongs to that function
//
// DTT keyed source contracts by a function's bare name: a same-named function
// in another namespace, an overload, or a method of the same name got another
// function's ensures, so a false fact became a proven violation.

struct Vec {
    int *data;
    int len;
};
[[clang::annotate("dtt:requires=len > 0")]] void needsNonEmpty(Vec &v);
[[clang::annotate("dtt:requires=len == 0")]] void needsEmpty(Vec &v);

namespace a {
[[clang::annotate("dtt:ensures=arg0.len == 10")]] void fill(Vec &v);
}
namespace b {
void fill(Vec &v);  // same name, no contract
}

// Variant 1: the annotated function's contract still applies.
void variant1_annotated() {
    Vec v{nullptr, 0};
    a::fill(v);
    needsEmpty(v);  // dtt-error {{len == 0}}
}

// Variant 2: an unannotated same-named function proves nothing.
void variant2_other_namespace() {
    Vec v{nullptr, 0};
    b::fill(v);
    needsNonEmpty(v);  // dtt-error {{cannot verify}}
}

int main() { return 0; }
