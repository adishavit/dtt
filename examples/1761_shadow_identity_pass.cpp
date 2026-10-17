// 1761: A declaration is its own variable (safe uses)
#include <cstdio>
#include <cstdlib>
#include <vector>

[[clang::annotate("dtt:requires=n >= 0")]] void take(int n);
int get();

// Variant 1: an inner negative does not reach the outer variable.
void variant1_outer_safe() {
    int n = 5;
    {
        int n = -1;
        (void)n;
    }
    take(n);
}

// Variant 2: an inner empty container does not empty the outer one.
void variant2_outer_container() {
    std::vector<int> v(3);
    {
        std::vector<int> v;
        (void)v;
    }
    v[2] = 1;
}

// Variant 3: an inner null pointer does not null the outer one.
void variant3_outer_pointer() {
    int x;
    int *p = &x;
    {
        int *p = nullptr;
        (void)p;
    }
    *p = 2;
}

// Variant 4: both files are closed.
void variant4_both_closed() {
    FILE *f = fopen("a", "r");
    if (!f) return;
    {
        FILE *f = fopen("b", "r");
        if (f) fclose(f);
    }
    fclose(f);
}

// Variant 5: freeing the inner pointer leaves the outer one valid.
void variant5_inner_freed() {
    int *p = static_cast<int *>(malloc(4));
    if (!p) return;
    {
        int *p = static_cast<int *>(malloc(4));
        free(p);
    }
    *p = 2;
    free(p);
}

// Variant 6: a parameter shadowed in a block.
void variant6_parameter(int n) {
    if (n < 0) return;
    {
        int n = -1;
        (void)n;
    }
    take(n);
}

// Variant 7: a variable declared again in a later block starts fresh.
void variant7_sibling() {
    {
        int k = get();
        if (k < 0) return;
        take(k);
    }
    {
        int k = 4;
        take(k);
    }
}

// Variant 8: a reference that shadows a variable is its referent: facts are
// read through it, and the outer variable keeps its own.
int variant8_shadowing_reference() {
    int *p = nullptr;
    int x = 1;
    int *y = &x;
    {
        int *&p = y;
        *p = 2;
    }
    return p ? *p : *y;
}

// Variant 9: a write through a shadowing reference reaches its referent.
int variant9_write_through_shadowing_reference() {
    int x = 1;
    int *p = &x;
    int *y = nullptr;
    {
        int *&p = y;
        p = &x;
    }
    return *y + *p;
}

int main() { return 0; }
