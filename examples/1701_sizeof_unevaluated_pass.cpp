// 1701: sizeof on unevaluated operands - DTT correctly skips these
// sizeof (except on VLAs) does not evaluate its operand - only the type
// matters. DTT correctly skips analysis inside sizeof, so it does not flag
// dereferences inside sizeof as null-deref or use-of-uninitialized.
// Found by scanning libwebp (src/enc/webp_enc.c, src/enc/token_enc.c).

#include <stdlib.h>
#include <stdint.h>

struct Preds { int data[16]; };
struct Info   { int x, y; };

struct Encoder {
    Preds*   preds;
    Info*    mb_info;
    uint8_t* y_top;
    int      num_parts;
};

// Variant 1: sizeof(*uninit_ptr) for allocation sizing (libwebp InitVP8Encoder)
void variant1_sizeof_uninit_ptr() {
    Encoder* enc;  // uninitialized
    size_t preds_size = 4 * 4 * sizeof(*enc->preds);
    size_t info_size  = 4 * 4 * sizeof(*enc->mb_info);
    size_t sample_size = 2 * 16 * sizeof(*enc->y_top);
    (void)preds_size;
    (void)info_size;
    (void)sample_size;
}

// Variant 2: sizeof(*null_ptr) for allocation (libwebp TBufferNewPage)
struct Tokens { struct Tokens* next; int data[64]; };

void variant2_sizeof_null_ptr() {
    Tokens* page = NULL;
    size_t size = sizeof(*page) + 64 * sizeof(int);
    page = (Tokens*)malloc(size);
    if (page) {
        page->next = NULL;
        free(page);
    }
}

// Variant 3: sizeof applied to array element via pointer
void variant3_sizeof_array_element() {
    int* arr;  // uninitialized
    size_t elem_size = sizeof(*arr);
    size_t total = 10 * elem_size;
    arr = (int*)malloc(total);
    if (arr) free(arr);
}

// Variant 4: sizeof in macro expansion (common pattern: memcpy sizing)
void variant4_sizeof_in_macro_like_expr() {
    double* src;  // uninitialized
    size_t copy_size = 100 * sizeof(*src);
    src = (double*)malloc(copy_size);
    if (src) free(src);
}

// Variant 5: nested member access inside sizeof
struct Outer {
    struct Inner {
        int values[32];
    } *inner;
};

void variant5_sizeof_nested_member() {
    Outer* o;  // uninitialized
    size_t sz = sizeof(*o->inner) + sizeof(o->inner->values[0]) * 32;
    (void)sz;
}

int main() {
    variant1_sizeof_uninit_ptr();
    variant2_sizeof_null_ptr();
    variant3_sizeof_array_element();
    variant4_sizeof_in_macro_like_expr();
    variant5_sizeof_nested_member();
    return 0;
}
