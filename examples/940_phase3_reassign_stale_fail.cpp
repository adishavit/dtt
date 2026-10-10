// 940_phase3_reassign_stale_fail.cpp
// Tests: Phase 3 gap - stale base origin after pointer reassignment
//
// When p = base + off, then p = other_base, the base origin for p
// should be cleared/updated. If stale, precondition uses wrong base.

#include <cstddef>
#include <cstring>
#include <cstdlib>

// Inner function - infers precondition on first param
void inner(char* buf, size_t n) {
    memcpy(buf, "data", n);
}

// Variant 1: Reassign to different parameter
void variant1_reassign_to_param(char* base1, char* base2, size_t off, size_t n) {
    char* p = base1 + off;  // p has base origin: base1, offset: off
    p = base2;              // REASSIGN: should update base origin to base2
    // Expected: base2._size_bytes >= n (not base1._size_bytes >= n+(off))
    inner(p, n);  // dtt-error {{call\-site precondition 'base2\._size_bytes >= n'}}
}

// Variant 2: Reassign to local array
void variant2_reassign_to_local(char* base, size_t off, size_t n) {
    char* p = base + off;   // p has base origin: base, offset: off
    char local[100];
    p = local;              // REASSIGN: should clear base origin (local is not param)
    // Expected: cannot verify (local not a param) or local._size_bytes >= n
    inner(p, n);  // dtt-error {{call\-site precondition 'buf\._size_bytes >= n'}}
}

// Variant 3: Reassign to malloc result
void variant3_reassign_to_malloc(char* base, size_t off, size_t n) {
    char* p = base + off;   // p has base origin: base, offset: off
    p = (char*)malloc(n);   // REASSIGN: should clear base origin
    if (!p) return;
    // Expected: cannot verify (malloc not tracked as param origin)
    inner(p, n);
    free(p);
}

// Variant 4: Reassign with arithmetic on new base
void variant4_reassign_with_arith(char* base1, char* base2, size_t off1, size_t off2, size_t n) {
    char* p = base1 + off1; // p has base origin: base1, offset: off1
    p = base2 + off2;       // REASSIGN: should update to base2, offset: off2
    // Expected: base2._size_bytes >= n+(off2)
    inner(p, n);  // dtt-error {{call\-site precondition 'base2\._size_bytes >= n\+\(off2\)'}}
}

// Variant 5: Chain of reassignments
void variant5_chain_reassign(char* b1, char* b2, char* b3, size_t off, size_t n) {
    char* p = b1 + off;     // origin: b1, off
    p = b2;                 // origin: b2, 0
    p = b3 + off;           // origin: b3, off
    // Expected: b3._size_bytes >= n+(off)
    inner(p, n);  // dtt-error {{call\-site precondition 'b3\._size_bytes >= n\+\(off\)'}}
}

int main() { return 0; }
