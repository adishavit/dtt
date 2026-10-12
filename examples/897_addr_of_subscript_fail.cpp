// 897: &E[i] past one-past-the-end is still an out-of-bounds pointer
//
// Forming a pointer beyond one past the end is undefined behavior even
// without a read, and reading element `size` is still an access error.

#include <cstddef>

// Variant 1: constant offset beyond one-past-the-end
void variant1_past_end() {
    char buf[16];
    char *p = &buf[17];  // dtt-error {{out-of-bounds pointer}}
    (void)p;
}

// Variant 2: reading one-past-the-end is an access, not pointer formation
char variant2_read_end() {
    char buf[16] = {};
    return buf[16];  // dtt-error {{out of bounds}}
}

// Variant 3: unguarded symbolic offset may exceed one-past-the-end
void variant3_symbolic(size_t i) {
    char buf[16];
    char *p = &buf[i];  // dtt-error {{out-of-bounds pointer}}
    (void)p;
}

// Variant 4: guard allows one too many
void variant4_off_by_one(size_t i) {
    char buf[16];
    if (i <= 17) {
        char *p = &buf[i];  // dtt-error {{out-of-bounds pointer}}
        (void)p;
    }
}

int main() { return 0; }
