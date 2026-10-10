// Example 1001: Partial branch leak detection (XFAIL - requires CFG analysis)
// Resource closed in one branch but not the other
//
// This is a known gap: DTT doesn't track that a field might be in different
// states depending on which branch was taken. Proper detection requires
// control flow graph analysis to merge states at join points.

struct FILE_handle {};

[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
FILE_handle* my_fopen(const char* path, const char* mode);

[[clang::annotate("dtt:requires=arg0.@open == 1")]]
[[clang::annotate("dtt:ensures=arg0.@open == 0")]]
int my_fclose(FILE_handle* f);

// Variant 1: Close only in if-branch
// After the if, f.@open could be 0 or 1 depending on flag
void variant1_if_only(bool flag) {
    FILE_handle* f = my_fopen("data.txt", "r");  // dtt-error {{scope-exit constraint}}
    if (flag) {
        my_fclose(f);  // f.@open = 0
    }
    // ERROR: when flag==false, f is not closed (f.@open still 1)
}

// Variant 2: Close only in else-branch
// Similar issue - state depends on which branch was taken
void variant2_else_only(bool flag) {
    FILE_handle* f = my_fopen("data.txt", "r");  // dtt-error {{scope-exit constraint}}
    if (flag) {
        // Not closed in this branch (f.@open still 1)
    } else {
        my_fclose(f);  // f.@open = 0
    }
    // ERROR: when flag==true, f is not closed
}

int main() {
    return 0;
}
