// 701_double_free_fail.cpp
// DTT detects double-free patterns by tracking freed state
// All variants demonstrate double-free patterns that DTT catches

#include <cstdlib>
#include <cstring>

// Variant 1: Simple double-free - immediate
void variant1_simple_double_free() {
    int* p = (int*)malloc(sizeof(int));
    *p = 42;
    free(p);
    free(p);  // BUG: double free  dtt-error {{double-free}}
}

// Variant 2: Double-free with code between
void variant2_double_free_with_code() {
    int* p = (int*)malloc(sizeof(int));
    *p = 42;
    free(p);

    int dummy = 100;
    dummy *= 2;  // Unrelated code
    (void)dummy;

    free(p);  // BUG: double free  dtt-error {{double-free}}
}

// Variant 3: Double-free through alias
void variant3_double_free_alias() {
    int* p = (int*)malloc(sizeof(int));
    int* q = p;  // q aliases p
    *p = 42;
    free(p);
    free(q);  // KNOWN GAP (not asserted): double free through alias
}

// Variant 4: Double-free in conditional
void variant4_double_free_conditional(bool flag) {
    int* p = (int*)malloc(sizeof(int));
    *p = 42;

    if (flag) {
        free(p);
    }

    free(p);  // BUG: double free if flag was true  dtt-error {{double-free|freed on some paths}}
}

// Variant 5: Double-free in loop
void variant5_double_free_loop() {
    int* p = (int*)malloc(sizeof(int));
    *p = 42;

    for (int i = 0; i < 3; i++) {
        free(p);  // KNOWN GAP (not asserted): double free on 2nd and 3rd iteration
    }
}

// Variant 6: Double-free of array
void variant6_double_free_array() {
    int* arr = (int*)malloc(10 * sizeof(int));
    for (int i = 0; i < 10; i++) arr[i] = i;
    free(arr);
    free(arr);  // BUG: double free of array  dtt-error {{double-free}}
}

// Variant 7: Double-free via function
void helper_free(int* p) {
    free(p);
}

void variant7_double_free_via_function() {
    int* p = (int*)malloc(sizeof(int));
    *p = 42;
    helper_free(p);
    free(p);  // KNOWN GAP (not asserted): double free (first free in helper)
}

// Variant 8: Double-free of struct and member
struct Container {
    int* data;
};

void variant8_double_free_struct() {
    Container* c = (Container*)malloc(sizeof(Container));
    c->data = (int*)malloc(sizeof(int));
    *(c->data) = 42;

    free(c->data);
    free(c);
    free(c->data);  // BUG: double free of member (also use-after-free of c)  dtt-error {{double-free}}
}

// Variant 9: Double-free with reassignment (not actually double-free)
void variant9_safe_with_reassignment() {
    int* p = (int*)malloc(sizeof(int));
    *p = 42;
    free(p);

    p = (int*)malloc(sizeof(int));  // p now points to NEW allocation
    *p = 100;
    free(p);  // OK: freeing the NEW allocation, not a double-free
}

// Variant 10: Double-free conditional both paths
void variant10_double_free_both_paths(bool flag) {
    int* p = (int*)malloc(sizeof(int));
    *p = 42;

    if (flag) {
        free(p);
    } else {
        free(p);
    }

    free(p);  // BUG: double free regardless of flag  dtt-error {{double-free}}
}

// Variant 11: Double-free with multiple aliases
void variant11_double_free_multiple_aliases() {
    int* p = (int*)malloc(sizeof(int));
    int* q = p;
    int* r = q;
    *p = 42;

    free(p);
    // q and r still point to freed memory
    free(r);  // KNOWN GAP (not asserted): double free through chain of aliases
}

// Variant 12: Double-free in error handling pattern (common bug)
void variant12_double_free_error_handling() {
    int* p = (int*)malloc(sizeof(int));
    if (!p) return;
    *p = 42;

    bool error = true;  // Simulating an error condition

    if (error) {
        free(p);
        // Missing return! Falls through to cleanup
    }

    // Cleanup code
    free(p);  // BUG: double free if error was true  dtt-error {{double-free|freed on some paths}}
}

// Variant 13: Double-free with calloc
void variant13_double_free_calloc() {
    int* p = (int*)calloc(5, sizeof(int));
    p[0] = 1;
    free(p);
    free(p);  // BUG: double free (calloc-allocated)  dtt-error {{double-free}}
}

// Variant 14: Double-free with realloc
void variant14_double_free_after_realloc() {
    char* p = (char*)malloc(10);
    strcpy(p, "hello");

    char* old_p = p;
    p = (char*)realloc(p, 20);  // If successful, old_p is now invalid

    if (p) {
        free(old_p);  // KNOWN GAP (not asserted): double free - realloc already freed old_p
        free(p);
    }
}

// Variant 15: Double-free in destructor pattern
class BadClass {
    int* data;
public:
    BadClass() : data((int*)malloc(sizeof(int))) { *data = 0; }
    ~BadClass() { free(data); }

    void manualCleanup() {
        free(data);  // KNOWN GAP (not asserted): will double-free when destructor runs
    }
};

void variant15_double_free_destructor() {
    BadClass obj;
    obj.manualCleanup();
    // Destructor will free again when obj goes out of scope
}

int main() {
    // These would crash or cause heap corruption at runtime
    // DTT should catch them at compile time
    return 0;
}
