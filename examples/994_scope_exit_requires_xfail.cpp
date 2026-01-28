// Example 994: Scope-exit requires (~requires) - should produce DTT error
// Linear resource not consumed before scope exit
//
// Tests the ~requires annotation which specifies constraints that must hold
// when a variable goes out of scope (linear type-like resource tracking).

// Simulated C-style file API with scope-exit constraint
struct FILE_handle {};

// fopen: returns handle with @open == 1, requires @open == 0 at scope exit
[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
FILE_handle* my_fopen(const char* path, const char* mode);

// fclose: transitions @open from 1 to 0
// Note: Using void return to ensure postcondition applies to arg0
[[clang::annotate("dtt:requires=@open == 1")]]
[[clang::annotate("dtt:ensures=@open == 0")]]
void my_fclose(FILE_handle* f);

// Variant 1: Simple leak - no fclose
void variant1_simple_leak() {
    FILE_handle* f = my_fopen("data.txt", "r");
    // Missing my_fclose(f)
}  // ERROR: ~requires failed: @open == 1, need @open == 0

// Variant 2: Leak on early return
void variant2_early_return(bool error) {
    FILE_handle* f = my_fopen("data.txt", "r");
    if (error) {
        return;  // ERROR: f not closed on this path
    }
    my_fclose(f);
}

// Variant 3: Leak in one branch
void variant3_branch_leak(bool flag) {
    FILE_handle* f = my_fopen("data.txt", "r");
    if (flag) {
        my_fclose(f);
    }
    // ERROR: f not closed when flag == false
}

// Variant 4: Multiple resources, one leaked
void variant4_partial_cleanup() {
    FILE_handle* f1 = my_fopen("file1.txt", "r");
    FILE_handle* f2 = my_fopen("file2.txt", "r");
    my_fclose(f1);
    // ERROR: f2 not closed
}

// Variant 5: Nested scope leak
void variant5_nested_scope() {
    {
        FILE_handle* f = my_fopen("data.txt", "r");
        // ERROR: f goes out of scope without close
    }
}

int main() {
    return 0;
}
