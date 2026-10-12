// dtt-run: enforce
// 898: Pointers derived from member pointers and element addresses
//
// `p = s.member` and `p = &q[i]` give `p` the tracked properties of the
// source pointer (nullability, extent, offset), the same way `p = q` does.
// For `&q[i]` the offset grows by i.

#include <cstddef>
#include <cstdlib>
#include <cstring>

struct Data { int value; };
struct Holder { Data *ptr; };
struct Buffer { char *data; size_t len; };

// Variant 1: non-null member pointer stays non-null through a copy
void variant1_member_nonnull() {
    Holder h;
    h.ptr = new Data();
    Data *p = h.ptr;
    p->value = 1;
    delete p;
}

// Variant 2: member pointer carries its extent
void variant2_member_extent(char *out, size_t n, size_t k) {
    Buffer b;
    b.data = static_cast<char *>(malloc(n));
    b.len = n;
    if (!b.data) return;
    char *p = b.data;
    if (k <= n) memcpy(out, p, k);
    free(b.data);
}

// Variant 3: assignment form
void variant3_member_assign(char *out, size_t n) {
    Buffer b;
    b.data = static_cast<char *>(malloc(n));
    b.len = n;
    if (!b.data) return;
    char *p = nullptr;
    p = b.data;
    memcpy(out, p, n);
    free(b.data);
}

// Variant 4: &q[i] on a pointer starts i elements in
void variant4_element_address(char *out, size_t len) {
    char *data = static_cast<char *>(malloc(len));
    if (!data) return;
    if (len < 4) { free(data); return; }
    char *p = &data[1];
    memcpy(out, p, len - 1);
    free(data);
}

int main() { return 0; }
