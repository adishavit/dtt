// dtt-run: enforce
// 897: &E[i] forms a pointer; it does not read element i
//
// `&E[i]` is `E + i`. Like any pointer arithmetic it may point one past the
// end, so the valid range is [0, size], not [0, size). `&E[0]` is `E` itself
// and is valid even for an empty buffer.

#include <cstddef>
#include <cstdlib>

// Variant 1: &E[0] of a buffer whose size may be zero
void variant1_first_of_symbolic(size_t len) {
    unsigned char *data = static_cast<unsigned char *>(malloc(len));
    if (!data) return;
    unsigned char *p = &data[0];
    (void)p;
    free(data);
}

// Variant 2: one-past-the-end of an array
void variant2_end_of_array() {
    char buf[16];
    char *end = &buf[16];
    (void)end;
}

// Variant 3: one-past-the-end of a symbolic buffer
void variant3_end_of_symbolic(size_t len) {
    char *data = static_cast<char *>(malloc(len));
    if (!data) return;
    char *end = &data[len];
    (void)end;
    free(data);
}

// Variant 4: guarded symbolic index may reach the end
void variant4_guarded_end(size_t i) {
    char buf[16];
    if (i <= 16) {
        char *p = &buf[i];
        (void)p;
    }
}

int main() { return 0; }
