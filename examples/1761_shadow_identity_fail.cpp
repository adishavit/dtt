// 1761: A declaration is its own variable
//
// DTT keyed state by name: an inner declaration that shadows an outer one
// overwrote the outer variable's state, and nothing restored it after the
// inner scope ended.
#include <cstdio>
#include <cstdlib>
#include <vector>

[[clang::annotate("dtt:requires=n >= 0")]] void take(int n);
int get();
int g = -1;

// Variant 1: the outer value survives the inner scope.
void variant1_outer_kept() {
    int n = -1;
    {
        int n = 5;
        take(n);
    }
    take(n);  // dtt-error {{arg0 >= 0}}
}

// Variant 2: an outer unknown stays unknown.
void variant2_outer_unknown() {
    int n = get();
    {
        int n = 7;
        (void)n;
    }
    take(n);  // dtt-error {{cannot verify}}
}

// Variant 3: a guard on the inner variable says nothing about the outer.
void variant3_inner_guard() {
    int n = get();
    {
        int n = get();
        if (n < 0) return;
    }
    take(n);  // dtt-error {{cannot verify}}
}

// Variant 4: a local named like a global.
void variant4_global_shadowed() {
    {
        int g = 3;
        (void)g;
    }
    take(g);  // dtt-error {{cannot verify|arg0 >= 0}}
}

// Variant 5: containers and pointers.
void variant5_container() {
    std::vector<int> v;
    {
        std::vector<int> v(3);
        (void)v;
    }
    v[2] = 1;  // dtt-error {{cannot verify|out of bounds|_size}}
}
void variant5_pointer() {
    int *p = nullptr;
    {
        int x;
        int *p = &x;
        *p = 1;
    }
    *p = 2;  // dtt-error {{null}}
}

// Variant 6: resources.
void variant6_outer_leaks() {
    FILE *f = fopen("a", "r");  // dtt-error {{scope-exit|leak}}
    if (!f) return;
    {
        FILE *f = fopen("b", "r");
        if (f) fclose(f);
    }
}
void variant6_outer_freed() {
    int *p = static_cast<int *>(malloc(4));
    if (!p) return;
    free(p);
    {
        int *p = static_cast<int *>(malloc(4));
        if (p) {
            *p = 1;
            free(p);
        }
    }
    *p = 2;  // dtt-error {{use-after-free|freed}}
}

// Variant 7: a later declaration of the name starts fresh.
void variant7_sibling_fresh() {
    {
        int k = get();
        if (k < 0) return;
        take(k);
    }
    {
        int k = get();
        take(k);  // dtt-error {{cannot verify}}
    }
}

int main() { return 0; }
