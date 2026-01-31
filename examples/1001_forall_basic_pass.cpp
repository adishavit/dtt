// Value Concepts: Bounded forall quantifier - passing cases
// When forall is implemented, this should compile without errors
//
// Currently: These annotations are ignored (unknown syntax)
// After implementation: Constraints should be verified and pass

#include <cstddef>

// Function requires all elements positive
[[clang::annotate("dtt:requires=forall(i, 0, arg1, arg0[i] > 0)")]]
void process_positive(int* arr, size_t n) {
    (void)arr; (void)n;
}

// Function establishes all elements are zero
[[clang::annotate("dtt:ensures=forall(i, 0, arg1, arg0[i] == 0)")]]
void zero_init(int* arr, size_t n) {
    for (size_t i = 0; i < n; i++) {
        arr[i] = 0;
    }
}

// Function requires all elements in range [0, 256)
[[clang::annotate("dtt:requires=forall(i, 0, arg1, arg0[i] >= 0 && arg0[i] < 256)")]]
void process_bytes(unsigned char* arr, size_t n) {
    (void)arr; (void)n;
}

int main() {
    // Variant 1: Literal array with known positive values
    int positive[] = {1, 2, 3, 4, 5};
    process_positive(positive, 5);  // Should pass: all elements > 0

    // Variant 2: After zero_init, array is all zeros
    int buffer[10];
    zero_init(buffer, 10);
    // After this, forall(i, 0, 10, buffer[i] == 0) should be established

    // Variant 3: unsigned char is always in [0, 256)
    unsigned char bytes[] = {0, 127, 255};
    process_bytes(bytes, 3);  // Should pass: type guarantees range

    return 0;
}
