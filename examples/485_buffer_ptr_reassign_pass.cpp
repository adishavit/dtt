// Example 485: Pointer reassignment tracking — should COMPILE
// Tests that p = p + n and p += n correctly update offset

void test_reassign_valid() {
    int arr[10];
    int* p = arr;
    p = p + 5;      // offset becomes 5
    p[0] = 0;       // OK: access at offset 5
    p[4] = 0;       // OK: access at offset 9
}

void test_compound_assign_valid() {
    int arr[10];
    int* p = arr;
    p += 3;         // offset becomes 3
    p[0] = 0;       // OK: access at offset 3
    p[6] = 0;       // OK: access at offset 9
}

void test_subtract_valid() {
    int arr[10];
    int* p = arr + 8;  // offset 8
    p = p - 3;         // offset becomes 5
    p[0] = 0;          // OK: access at offset 5
    p[-5] = 0;         // OK: access at offset 0
}

void test_compound_subtract_valid() {
    int arr[10];
    int* p = arr + 9;
    p -= 4;            // offset becomes 5
    p[4] = 0;          // OK: access at offset 9
}

// Dynamic offset with loop proof
void test_reassign_loop_proof() {
    int arr[10];
    int* p = arr;
    for (int k = 0; k < 5; k++) {
        p = arr + k;   // offset = k (proven k < 5)
        p[4] = 0;      // OK: k + 4 < 5 + 4 = 9 < 10
    }
}

// Dynamic offset with compound assign in loop
void test_compound_assign_loop() {
    int arr[10];
    int* p = arr;
    for (int i = 0; i < 10; i++) {
        *p = i;        // OK: offset i < 10
        p += 1;        // p becomes arr + (i+1) for next iteration
    }
    // After loop: p = arr + 10, but we don't dereference
}

// Dynamic offset from prior pointer
void test_chain_reassign() {
    int arr[10];
    int* p = arr;
    int* q = p + 3;    // q has offset 3
    p = q + 2;         // p has offset 5
    p[4] = 0;          // OK: offset 9 < 10
}

// Runtime k with bounds check proof
void test_runtime_k_with_proof(int k) {
    int arr[10];
    int* p = arr;
    if (k >= 0 && k < 5) {
        p += k;        // offset = k (proven 0 <= k < 5)
        p[4] = 0;      // OK: k + 4 < 5 + 4 = 9 < 10
    }
}

// Runtime k in reassignment with proof
void test_runtime_k_reassign_proof(int k) {
    int arr[10];
    int* p = arr;
    if (k >= 0 && k < 10) {
        p = p + k;     // offset = k (proven 0 <= k < 10)
        *p = 0;        // OK: k < 10
    }
}

int main() {
    test_reassign_valid();
    test_compound_assign_valid();
    test_subtract_valid();
    test_compound_subtract_valid();
    test_reassign_loop_proof();
    test_compound_assign_loop();
    test_chain_reassign();
    test_runtime_k_with_proof(3);
    test_runtime_k_reassign_proof(5);
    return 0;
}
