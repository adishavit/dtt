// Example 613: Array element nullability from array guard
// ============================================================================
// WHAT IS TESTED
// ============================================================================
// When a pointer-to-pointer (T**) is guarded with a null check like:
//   if (arr == NULL) return error;
// DTT now propagates non-null to array elements (arr[i]) as well.
//
// This is a pragmatic heuristic: if the programmer guards the array pointer,
// they likely expect the array elements to be valid too.
//
// Expected: PASS (arr is guarded, so arr[i] elements are trusted)

struct Header {
    int compression_type;
    int num_channels;
};

void processHeaders(Header** headers, int count, int* err) {
    // Guard: if headers is NULL, return early
    if (headers == nullptr || count == 0) {
        *err = 1;
        return;
    }

    // After the guard, DTT marks headers[?]._nonnull = 1
    // So headers[i]->member is considered safe
    for (int i = 0; i < count; i++) {
        // PASS: headers[i] is trusted because headers was guarded
        if (headers[i]->compression_type < 0) {
            *err = 2;
            return;
        }

        // PASS: still safe in the loop
        for (int c = 0; c < headers[i]->num_channels; c++) {
            // Process channel c
        }
    }
}

int main() {
    Header h1{1, 3};
    Header h2{2, 5};
    Header* arr[2] = {&h1, &h2};
    int err = 0;

    processHeaders(arr, 2, &err);
    return err;
}
