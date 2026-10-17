// 1769: Quiet mode still reports what is proven (run in quiet mode).

#include <cstdlib>
#include <vector>

// Variant 1: a pointer known to be null, dereferenced.
void variant1_null() {
    int *p = nullptr;
    *p = 1;  // dtt-error {{null}}
}

// Variant 2: an index proven out of bounds.
void variant2_out_of_bounds() {
    int a[4] = {};
    a[4] = 1;  // dtt-error {{out of bounds|_size}}
}

// Variant 3: a pointer freed twice.
void variant3_double_free() {
    char *p = static_cast<char *>(malloc(4));
    free(p);
    free(p);  // dtt-error {{double|freed}}
}

// Variant 4: a requirement proven violated.
[[clang::annotate("dtt:requires=n >= 0")]] void take(int n);
void variant4_violated() {
    take(-1);  // dtt-error {{violated|arg0 >= 0}}
}

int main() { return 0; }
