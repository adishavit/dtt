// Example 996: Path-sensitive scope-exit - xfail (DTT lacks path sensitivity)
// These variants require control-flow-aware analysis to verify correctly.
// DTT currently uses a flow-insensitive approach where postconditions are applied
// without understanding which branch was taken.

struct FILE_handle {};

[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
FILE_handle* my_fopen(const char* path, const char* mode);

[[clang::annotate("dtt:requires=@open == 1")]]
[[clang::annotate("dtt:ensures=@open == 0")]]
void my_fclose(FILE_handle* f);

// Variant 1: Branch-based cleanup (requires path sensitivity)
// DTT doesn't understand that the else branch hasn't called my_fclose yet
void variant1_both_branches(bool error) {
    FILE_handle* f = my_fopen("data.txt", "r");
    if (error) {
        my_fclose(f);
        return;
    }
    my_fclose(f);  // DTT thinks @open is already 0 from line above
}

// Variant 2: Nested scope with cleanup (requires scope sensitivity)
void variant2_nested_cleanup() {
    {
        FILE_handle* f = my_fopen("data.txt", "r");
        my_fclose(f);
    }  // Inner scope ends here
}

// Variant 3: Conditional open with conditional close (requires alias + path sensitivity)
void variant3_conditional_open(bool need_file) {
    FILE_handle* f = nullptr;
    if (need_file) {
        f = my_fopen("data.txt", "r");
    }
    if (f) {
        my_fclose(f);
    }
}

int main() {
    return 0;
}
