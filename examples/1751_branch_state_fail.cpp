// 1751: Branch state - facts learned in a branch do not outlive it
//
// Each marked line can fail. DTT used to keep facts learned inside a branch
// (a guard, a resource release) after the branch, or to forget a branch
// entirely, and was silent or claimed a false fact.
#include <cstdio>
#include <cstdlib>
#include <vector>

int g;
int *get();
bool cond();

// Variant 1: a null check inside a branch says nothing after it.
int variant1_guard_in_branch() {
    int *p = get();
    if (cond()) {
        if (!p) return 0;
    }
    return *p;  // dtt-error {{nullability unknown}}
}

// Variant 2: an emptiness check inside a branch says nothing after it.
int variant2_size_guard_in_branch(std::vector<int> &v) {
    if (cond()) {
        if (v.empty()) return 0;
    }
    return v[0];  // dtt-error {{cannot verify|unknown|out of bounds}}
}

// Variant 3: p is null on one path only; it is not "known to be null".
int variant3_or_guard_maybe_null(int n) {
    int *p = &g;
    if (n < 0 || n > 9) {
        p = nullptr;
    }
    return *p;  // dtt-error {{nullability unknown}}
}

// Variant 4: the then branch sets p to null; the else branch does not.
int variant4_or_guard_with_else(int n) {
    int *p = &g;
    if (n < 0 || n > 9) {
        p = nullptr;
    } else {
        g = 1;
    }
    return *p;  // dtt-error {{nullability unknown}}
}

// Variant 5: returning the buffer on one path does not release it on the other.
void *variant5_leak_on_other_path(int c) {
    void *p = malloc(4);  // dtt-error {{scope-exit|leak}}
    if (!p) return nullptr;
    if (c) return p;
    return nullptr;
}

// Variant 6: freed on one path; a later use or free is not proven safe.
int variant6_maybe_freed_use(int c) {
    int *p = (int *)malloc(sizeof(int));
    if (!p) return 0;
    *p = 1;
    if (c) free(p);
    int r = *p;  // dtt-error {{freed on some paths}}
    if (!c) free(p);
    return r;
}

// Variant 7: freed on one path, then freed again.
void variant7_maybe_double_free(int c) {
    int *p = (int *)malloc(sizeof(int));
    if (!p) return;
    if (c) free(p);
    free(p);  // dtt-error {{freed on some paths}}
}

// Variant 8: dropping one handle in each branch does not close the file
// on the path that closes nothing.
void variant8_dropped_handle_leak(int c) {
    FILE *f = fopen("a", "r");
    if (!f) return;
    FILE *g = f;  // dtt-error {{scope-exit|leak}}
    if (c) {
        g = nullptr;
        fclose(f);
    } else {
        f = nullptr;
    }
}

// Variant 9: the freed node is read while advancing the list.
struct Node { Node *next; };
Node *variant9_read_after_free(Node *n) {
    Node *head = (Node *)malloc(sizeof(Node));
    if (!head) return n;
    head->next = n;
    free(head);
    head = head->next;  // dtt-error {{use-after-free}}
    return head;
}

// Variant 10: allocated on one path, freed twice after the branch.
void variant10_allocated_on_one_path(int c) {
    void *p = nullptr;
    if (c) p = malloc(4);
    free(p);
    free(p);  // dtt-error {{double-free|freed on some paths}}
}

// Variant 11: freed on one path, used after the branch.
void variant11_freed_on_one_path(int c, int *q) {
    int *p = q;
    if (c) {
        p = (int *)malloc(sizeof(int));
        if (!p) return;
        free(p);
    }
    if (p) *p = 1;  // dtt-error {{freed on some paths}}
}

// Variant 12: the file is closed on one path only.
void variant12_closed_on_one_path(int c) {
    FILE *f = fopen("a", "r");  // dtt-error {{scope-exit|leak}}
    if (c) {
        if (!f) return;
        fclose(f);
    }
}

// Variant 13: allocated with new[] on one path, released with free().
void variant13_mismatch_on_one_path(int c) {
    int *p = nullptr;
    if (c) p = new int[4];
    free(p);  // dtt-error {{mismatched}}
}

int main() { return 0; }
