// 1766: Values flow through assignments, references, calls and swaps (safe uses)

#include <cstdlib>
#include <utility>
#include <vector>

[[clang::annotate("dtt:requires=n >= 0")]] void take(int n);

// Variant 1: a temporary's size replaces the old one.
int variant1_assign_temporary() {
    std::vector<int> v;
    v = std::vector<int>(3);
    return v[2];
}

// Variant 2: a write through a reference is the variable's value.
void variant2_reference_write() {
    int n = -1;
    int &r = n;
    r = 5;
    take(n);
}

// Variant 3: swapped scalars.
void variant3_swap() {
    int a = -1, b = 5;
    std::swap(a, b);
    take(a);
}

// Variant 4: a cast malloc result checked for null.
void variant4_cast_malloc() {
    char *p = (char *)malloc(16);
    if (!p) return;
    p[15] = 0;
    free(p);
}

// Variant 5: a local the call cannot reach keeps its value.
void opaque();
void variant5_local_across_call() {
    int n = 5;
    opaque();
    take(n);
}

// Variant 6: a local whose address is taken keeps its value until something
// may write through it.
void variant6_address_taken_unwritten() {
    int n = 5;
    int *p = &n;
    (void)p;
    take(n);
}

// Variant 7: a constant static local.
void variant7_const_static() {
    static const int limit = 5;
    take(limit);
}

// Variant 8: a call to a function that writes no globals keeps them.
int g_kept;
int square(int x) { return x * x; }
void variant8_global_kept() {
    g_kept = 5;
    (void)square(3);
    take(g_kept);
}

int main() { return 0; }
