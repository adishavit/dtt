// 898: Derived pointers keep the source's limits
//
// A pointer copied from a member or formed as &q[i] must not be treated as
// larger, or less null, than its source.

#include <cstddef>
#include <cstdlib>
#include <cstring>

struct Data { int value; };
struct Holder { Data *ptr; };
struct Buffer { char *data; size_t len; };

// Variant 1: member pointer extent bounds the copy
void variant1_member_overread(char *out, size_t n) {
    Buffer b;
    b.data = static_cast<char *>(malloc(n));
    b.len = n;
    if (!b.data) return;
    char *p = b.data;
    memcpy(out, p, n + 1);  // dtt-error {{over-read}}
    free(b.data);
}

// Variant 2: &q[i] leaves i fewer elements
void variant2_element_overread(char *out, size_t len) {
    char *data = static_cast<char *>(malloc(len));
    if (!data) return;
    if (len < 2) { free(data); return; }
    char *p = &data[2];
    memcpy(out, p, len);  // dtt-error {{over-read}}
    free(data);
}

// Variant 3: null member pointer stays null through a copy
void variant3_member_null() {
    Holder h;
    h.ptr = nullptr;
    Data *p = h.ptr;
    p->value = 1;  // dtt-error {{known to be null}}
}

// Variant 4: assignment form keeps the extent
void variant4_assign_overread(char *out, size_t n) {
    Buffer b;
    b.data = static_cast<char *>(malloc(n));
    b.len = n;
    if (!b.data) return;
    char *p = nullptr;
    p = b.data;
    memcpy(out, p, n + 1);  // dtt-error {{over-read}}
    free(b.data);
}

int main() { return 0; }
