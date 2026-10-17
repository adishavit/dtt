// 1758: Loops - safe loops stay accepted with the state of every iteration
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <vector>

[[clang::annotate("dtt:requires=n < 3")]] void lt3(int n);
bool c();
int a[4];
int g;

// Variant 1: a counting-down loop stays in bounds.
int variant1_count_down(void) {
    int s = 0;
    for (int k = 3; k >= 0; --k) s += a[k];
    return s;
}

// Variant 2: k is in [1, 3].
void variant2_count_down_strict(void) {
    for (int k = 3; k > 0; --k) lt3(k - 1);
}

// Variant 3: a pointer allocated and freed on each iteration.
void variant3_alloc_per_iteration(int n) {
    for (int i = 0; i < n; i++) {
        int *p = (int *)malloc(sizeof(int));
        if (!p) return;
        *p = i;
        free(p);
    }
}

// Variant 4: a file opened and closed on each iteration.
void variant4_file_per_iteration(int n) {
    for (int i = 0; i < n; i++) {
        FILE *f = fopen("a", "r");
        if (!f) continue;
        fgetc(f);
        fclose(f);
    }
}

// Variant 5: a fact the loop does not touch survives it.
int variant5_untouched_fact(int *p, int n) {
    if (!p) return 0;
    for (int i = 0; i < n; i++) g += i;
    return *p;
}

// Variant 6: a container the loop only reads keeps its size.
int variant6_read_only_container(int n) {
    std::vector<int> v(5);
    for (int i = 0; i < n; i++) g += v[0];
    return v[4];
}

// Variant 7: a list walk.
struct Node { Node *next; int v; };
int variant7_list_walk(Node *head) {
    int s = 0;
    for (Node *n = head; n;) {
        s += n->v;
        Node *next = n->next;
        n = next;
    }
    return s;
}

// Variant 8: an infinite loop left by break.
int variant8_break_out(int *p) {
    for (;;) {
        if (p) break;
        p = &g;
    }
    return *p;
}

// Variant 9: a bound held by a variable with a known value.
int variant9_variable_bound() {
    int arr[5] = {};
    int s = 0;
    for (int i = 0, end = 5; i < end; i++) s += arr[i];
    int n = 5;
    for (int i = 0; i < n; i++) s += arr[i];
    int j = 0;
    while (j < n) { s += arr[j]; ++j; }
    return s;
}

// Variant 10: a guard against a variable with a known value.
int variant10_variable_guard(int i) {
    int arr[5] = {};
    int n = 5;
    if (i >= 0 && i < n) return arr[i];
    return 0;
}

// Variant 11: pointer iteration up to an end pointer.
int variant11_pointer_iteration() {
    int arr[10] = {};
    int s = 0;
    int *end = arr + 10;
    for (int *p = arr; p < end; p++) s += *p;
    for (int *p = arr; p != end; ++p) s += *p;
    for (int *p = arr; p < arr + 10; ++p) s += *p;
    for (int *p = arr + 2; p < end; ++p) s += *p;
    return s;
}

// Variant 12: a file kept by break or continue is closed after the loop.
void variant12_kept_by_break(int n) {
    FILE *f = nullptr;
    while (c()) {
        f = fopen("a", "r");
        if (!f) continue;
        break;
    }
    if (f) fclose(f);
    FILE *h = nullptr;
    for (int i = 0; i < n; i++) {
        h = fopen("a", "r");
        if (h) break;
    }
    if (h) fclose(h);
}

// Variant 13: a bound held by a field, through a cast.
struct Buf {
    int _size;
    [[clang::annotate("dtt:requires=arg0 < _size")]] int &operator[](int i);
};
void variant13_field_bound() {
    Buf b;
    b._size = 5;
    for (unsigned i = 0; i < static_cast<unsigned>(b._size); i++) b[static_cast<int>(i)] = 1;
    unsigned j = 0;
    while (j < static_cast<unsigned>(b._size)) {
        b[static_cast<int>(j)] = 1;
        j++;
    }
}

// Variant 14: a pointer stepped once per pass of a counted loop.
int variant14_stepped_pointer() {
    int arr[10] = {};
    int s = 0;
    int *p = arr;
    for (int i = 0; i < 10; i++) {
        s += *p;
        p++;
    }
    int five[5] = {};
    int *q = five;
    for (int i = 5; i < 10; i++) {
        *q = i;
        ++q;
    }
    return s;
}

// Variant 15: a descriptor reset after a failed open holds nothing.
void variant15_fd_reset_after_failed_open() {
    int fd = open("a", O_RDONLY);
    if (fd == -1) {
        fd = 0;
        return;
    }
    close(fd);
    int fd2 = open("a", O_RDONLY);
    if (fd2 <= -1) {
        fd2 = 0;
        return;
    }
    close(fd2);
}

// Variant 16: a handle reset after a failed acquisition, and a retry.
void variant16_reset_and_retry() {
    FILE *f = fopen("a", "r");
    if (!f) {
        f = nullptr;
        return;
    }
    fclose(f);
    FILE *g = fopen("a", "r");
    if (!g) {
        g = fopen("b", "r");
        if (!g) return;
    }
    fclose(g);
}

int main() { return 0; }
