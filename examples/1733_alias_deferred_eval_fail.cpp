// 1733: Alias-aware deferred eval — null check on alias triggers deferred postcondition
// When a resource pointer is aliased and the alias is null-checked, the deferred
// postcondition from the original fopen should be applied via the alias chain.

#include <cstdio>

// Variant 1: alias null-checked, original leaked
void variant1_alias_null_check_leak() {
    FILE* f = fopen("data.txt", "r");
    FILE* g = f;           // alias: g -> f
    if (g) {
        // Null check on alias g proves g._nonnull
        // Deferred postcondition on f triggers via alias → f.@open = 1
        int c = fgetc(f);  // dtt-error {{_nonnull}}
        (void)c;
        // forgot fclose
    }
    // ERROR: f.@open = 1, resource leak
}

// Variant 2: alias null-checked, close via original
void variant2_alias_null_check_close_original() {
    FILE* f = fopen("data.txt", "r");
    FILE* g = f;
    if (g) {
        fclose(f);  // close via original (not alias)  dtt-error {{_nonnull}}
    }
    // ERROR should NOT fire — f is properly closed
    // (but may still fire due to merge; that's the ghost merge gap)
}

int main() { return 0; }
