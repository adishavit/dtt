// 939_phase3_param_copy_origin_missing_xfail.cpp
// Gap: pointer copies from parameters (p = base) do not create a base origin,
// so inferred preconditions stay on local 'p' and never reach call sites.

#include <cstddef>
#include <cstring>

void inner_copy(char* base, size_t n) {
    char* p = base;  // Should inherit base origin.
    memset(p, 0, n);
}

void inner_copy_then_offset(char* base, size_t off, size_t n) {
    char* p = base;  // Should inherit base origin.
    p += off;
    memset(p, 0, n);
}

void inner_copy_reassign(char* base, size_t off, size_t n) {
    char* p = base;  // Should inherit base origin.
    p = p + off;
    memset(p, 0, n);
}

// Variant 1: direct copy should create a call-site precondition.
void outer_copy_too_small() {
    char buf[8];
    inner_copy(buf, 12);  // Should warn: 8 < 12  dtt-error {{call\-site precondition 'base\._size_bytes >= n'}}
}

// Variant 2: copy then offset should still rewrite to base.
void outer_copy_offset_too_small() {
    char buf[16];
    inner_copy_then_offset(buf, 8, 10);  // Should warn: 16 < 10 + 8  dtt-error {{call\-site precondition 'base\._size_bytes >= n\+\(off\)'}}
}

// Variant 3: copy then reassignment form.
void outer_copy_reassign_too_small() {
    char buf[16];
    inner_copy_reassign(buf, 8, 10);  // Should warn: 16 < 10 + 8  dtt-error {{call\-site precondition 'base\._size_bytes >= n\+\(off\)'}}
}

int main() { return 0; }
