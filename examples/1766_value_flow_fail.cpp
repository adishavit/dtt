// 1766: Values flow through assignments, references, calls and swaps
//
// DTT kept a variable's old facts when it was assigned from a temporary or a
// call, did not see writes through a local reference or pointer, kept a
// global's facts across calls that write it, did not swap scalar values, and
// did not move a pointer's lifetime with an assignment.

#include <cstdlib>
#include <utility>
#include <vector>

[[clang::annotate("dtt:requires=n >= 0")]] void take(int n);
std::vector<int> make();

// Variant 1: assignment from a call replaces the old size.
int variant1_assign_from_call() {
    std::vector<int> v(3);
    v = make();
    return v[2];  // dtt-error {{cannot verify|_size|out of bounds}}
}

// Variant 2: assignment from a temporary replaces the old size.
int variant2_assign_from_temporary() {
    std::vector<int> v(3);
    v = std::vector<int>();
    return v[0];  // dtt-error {{out of bounds|cannot verify|_size}}
}

// Variant 3: a write through a reference writes the variable.
void variant3_write_through_reference() {
    int n = 5;
    int &r = n;
    r = -1;
    take(n);  // dtt-error {{_value|cannot verify}}
}

// Variant 4: a write through a pointer writes the variable.
void variant4_write_through_pointer() {
    int n = 5;
    int *p = &n;
    *p = -1;
    take(n);  // dtt-error {{_value|cannot verify}}
}

// Variant 5: a call may write a global.
int g_count;
void reset_count();
void variant5_global_written_by_call() {
    g_count = 5;
    reset_count();
    take(g_count);  // dtt-error {{_value|cannot verify}}
}

// Variant 6: swapping scalars swaps their values.
void variant6_swap_scalars() {
    int a = 5, b = -1;
    std::swap(a, b);
    take(a);  // dtt-error {{_value|cannot verify}}
}

// Variant 7: a pointer assigned from a freed one is freed.
void variant7_lifetime_transfer() {
    int *p = (int *)malloc(sizeof(int));
    if (!p) return;
    int *q = nullptr;
    free(p);
    q = p;
    *q = 1;  // dtt-error {{freed|use-after-free|cannot verify}}
}

// Variant 8: a reference to a container sees its clear().
int variant8_reference_container() {
    std::vector<int> v(3);
    std::vector<int> &r = v;
    r.clear();
    return v[2];  // dtt-error {{out of bounds|cannot verify|_size}}
}

// Variant 9: a static local's initializer runs once; later calls see what
// earlier ones left.
void variant9_static_local() {
    static int left = 5;
    take(left);  // dtt-error {{_value|cannot verify}}
    left = -1;
}

// Variant 10: a pointer to a local, written through after a call.
void fill(int *out);
void variant10_pointer_escapes_into_call() {
    int n = 5;
    int *p = &n;
    fill(p);
    take(n);  // dtt-error {{_value|cannot verify}}
}

// Variant 11: a global loop variable a called function writes.
int g_i;
int arr10[10];
void bump() { g_i = 100; }
int variant11_global_counter() {
    int s = 0;
    for (g_i = 0; g_i < 10; ++g_i) {
        bump();
        s += arr10[g_i];  // dtt-error {{cannot verify|out of bounds}}
    }
    return s;
}

// Variant 12: a const reference to a converted value is bound to a
// temporary copy: later writes to the source do not reach it.
[[clang::annotate("dtt:requires=n >= 0")]] void takeLong(long n);
void variant12_converted_reference() {
    int n = -1;
    const long &r = n;
    n = 5;
    takeLong(r);  // dtt-error {{cannot verify|violated|_value}}
}

// Variant 13: the same through a member and a materialized temporary.
struct Pair { int a; };
void variant13_converted_member(Pair p) {
    p.a = -1;
    const long &r = p.a;
    p.a = 5;
    takeLong(r);  // dtt-error {{cannot verify|violated|_value}}
}

// Variant 14: a const reference to a prvalue is its own object.
void variant14_prvalue_reference(int m) {
    const int &r = m + 0;
    m = 5;
    take(r);  // dtt-error {{cannot verify|violated|_value}}
}

// Variant 15: a redeclaration that names the parameters in another order;
// the requirement is over the definition's parameters.
#include <cstring>
void fillLast(char *b, int m, int n) { memset(b, 0, static_cast<unsigned>(n)); (void)m; }
void fillLast(char *b, int n, int m);
void variant15_redeclared_names() {
    char buf[4];
    fillLast(buf, 2, 100);  // dtt-error {{precondition|too small}}
}

// Variant 16: a bound proven of an index belongs to its value: after a swap
// it holds of the other variable.
int variant16_swapped_index(std::vector<int> &v, size_t i, size_t j) {
    if (i < v.size()) {
        std::swap(i, j);
        return v[i];  // dtt-error {{cannot verify|_size|out of bounds}}
    }
    return 0;
}

// Variant 17: an overloaded operator taking the index by reference writes it.
#include <iostream>
int variant17_extracted_index(std::vector<int> &v, size_t i) {
    if (i < v.size()) {
        std::cin >> i;
        return v[i];  // dtt-error {{cannot verify|_size|out of bounds}}
    }
    return 0;
}

// Variant 18: the same for a plain scalar bound.
void variant18_extracted_value(int n) {
    if (n >= 0) {
        std::cin >> n;
        take(n);  // dtt-error {{cannot verify|_value}}
    }
}

int main() { return 0; }
