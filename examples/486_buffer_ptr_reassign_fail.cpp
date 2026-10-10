// Example 486: Pointer reassignment OOB — should FAIL
// Tests that p = p + n and p += n catch OOB access

void test_reassign_oob() {
    int arr[5];
    int* p = arr;
    p = p + 10;     // offset becomes 10
    *p = 0;         // FAIL: offset 10 >= size 5  dtt-error {{out of bounds}}
}

void test_compound_assign_oob() {
    int arr[5];
    int* p = arr;
    p += 10;        // offset becomes 10
    *p = 0;         // FAIL: offset 10 >= size 5  dtt-error {{out of bounds}}
}

void test_reassign_then_index_oob() {
    int arr[10];
    int* p = arr;
    p = p + 5;      // offset becomes 5
    p[5] = 0;       // FAIL: access at offset 10 >= size 10  dtt-error {{out of bounds}}
}

void test_subtract_oob() {
    int arr[10];
    int* p = arr + 2;
    p = p - 5;      // offset becomes -3
    *p = 0;         // FAIL: negative offset  dtt-error {{out of bounds}}
}

// Dynamic offset with loop proof but still OOB
void test_loop_proof_oob() {
    int arr[5];
    int* p = arr;
    for (int k = 0; k < 5; k++) {
        p = arr + k;   // offset = k (proven k < 5)
        p[5] = 0;      // FAIL: k + 5 >= 5, always OOB  dtt-error {{out of bounds}}
    }
}

// Compound assign in loop that goes OOB
void test_compound_loop_oob() {
    int arr[5];
    int* p = arr;
    for (int i = 0; i < 10; i++) {  // Loop goes too far
        *p = i;        // KNOWN GAP (not asserted): when i >= 5, offset >= 5
        p += 1;
    }
}

// Chain reassignment that goes OOB
void test_chain_oob() {
    int arr[10];
    int* p = arr;
    int* q = p + 5;    // q has offset 5
    p = q + 6;         // p has offset 11
    *p = 0;            // FAIL: offset 11 >= size 10  dtt-error {{out of bounds}}
}

// Runtime k without proof - cannot verify safety
void test_runtime_k_no_proof(int k) {
    int arr[10];
    int* p = arr;
    p += k;            // offset = k (no bounds proof)
    *p = 0;            // FAIL: k unbounded  dtt-error {{out of bounds}}
}

// Runtime k with insufficient proof
void test_runtime_k_insufficient_proof(int k) {
    int arr[5];
    int* p = arr;
    if (k >= 0 && k < 10) {  // Proof allows k up to 9
        p = p + k;     // offset = k
        *p = 0;        // FAIL: k could be >= 5  dtt-error {{out of bounds}}
    }
}

int main() {
    test_reassign_oob();
    test_compound_assign_oob();
    test_reassign_then_index_oob();
    test_subtract_oob();
    test_loop_proof_oob();
    test_compound_loop_oob();
    test_chain_oob();
    test_runtime_k_no_proof(5);
    test_runtime_k_insufficient_proof(7);
    return 0;
}
