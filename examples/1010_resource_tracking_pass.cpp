// Example 1010: Resource-centric tracking with multiple aliases
// Tests that DTT tracks THE RESOURCE, not just variables
// Multiple handles can point to the same resource; releasing one is OK if others remain

struct FILE_handle {};

[[clang::annotate("dtt:ensures=result.@open == 1")]]
[[clang::annotate("dtt:~requires=result.@open == 0")]]
FILE_handle* my_fopen(const char* path, const char* mode);

[[clang::annotate("dtt:requires=arg0.@open == 1")]]
[[clang::annotate("dtt:ensures=arg0.@open == 0")]]
int my_fclose(FILE_handle* f);

// Variant 1: Release alias before close via other - should be OK
// Current behavior: ERROR (move semantics sees g=nullptr as leak)
// Desired behavior: OK (g just releases handle, resource closed via f)
void variant1_release_then_close(bool cond) {
    FILE_handle* f = my_fopen("data.txt", "r");
    FILE_handle* g = f;
    if (cond) {
        g = nullptr;   // Release g's handle to resource
        my_fclose(f);  // Close resource via f
    } else {
        f = nullptr;   // Release f's handle to resource
        my_fclose(g);  // Close resource via g
    }
}

// Variant 2: Transitive aliases - all point to same resource
// h = g = f, close via any should work
void variant2_transitive_alias() {
    FILE_handle* f = my_fopen("data.txt", "r");
    FILE_handle* g = f;
    FILE_handle* h = g;  // h -> g -> f, all same resource
    f = nullptr;
    g = nullptr;
    my_fclose(h);  // Close resource via last remaining handle
}

// Variant 3: Release all but one, then close
void variant3_release_all_but_one() {
    FILE_handle* f = my_fopen("data.txt", "r");
    FILE_handle* g = f;
    FILE_handle* h = f;
    FILE_handle* i = g;  // i -> g -> f

    f = nullptr;  // Release
    g = nullptr;  // Release
    h = nullptr;  // Release
    my_fclose(i); // Close via last handle
}

int main() {
    return 0;
}
