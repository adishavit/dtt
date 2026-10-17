// 1753: Each function starts from an empty program state
//
// DTT kept pointer aliases and known array elements from the previous
// function, keyed by variable name. A same-named variable in the next function
// inherited them and was "proven" safe.
#include <cstddef>
#include <cstdlib>

[[clang::annotate("dtt:requires=forall(i, 0, n, arr[i] > 0)")]]
void requires_positive(const int* arr, size_t n);

// Variant 1: known elements of a local array...
void variant1_known_elements() {
    int arr[3] = {1, 2, 3};
    requires_positive(arr, 3);
}

// ...say nothing about a parameter with the same name.
void variant1_unknown_elements(int *arr) {
    requires_positive(arr, 3);  // dtt-error {{cannot verify|forall}}
}

// Variant 2: an alias from the previous function...
void variant2_alias(int *p) {
    if (!p) return;
    int *q = p;
    *q = 1;
}

// ...does not make a same-named pointer non-null.
void variant2_unrelated(int *p) {
    int *q = p;
    *q = 1;  // dtt-error {{null}}
}

int main() { return 0; }
