// Example 997: Basic scope-exit leak detection - should produce DTT error
// Tests simple linear resource leak at function exit (no branches, no nested scopes)
//
// This tests the core ~requires functionality: a resource acquired at function
// scope that is never released before the function returns.

// Simulated C-style file API with scope-exit constraint
struct FILE_handle {};

// fopen: returns handle with @open == 1, requires @open == 0 at scope exit
[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
FILE_handle* my_fopen(const char* path, const char* mode);

// fclose: transitions @open from 1 to 0
[[clang::annotate("dtt:requires=arg0.@open == 1")]]
[[clang::annotate("dtt:ensures=arg0.@open == 0")]]
int my_fclose(FILE_handle* f);

// Simple leak: open file, never close, return
// ERROR: scope-exit constraint violated: @open == 1, need @open == 0
void simple_leak() {
    FILE_handle* f = my_fopen("data.txt", "r");  // dtt-error {{scope-exit constraint}}
    // No my_fclose(f) - resource leaked!
}

int main() {
    simple_leak();
    return 0;
}
