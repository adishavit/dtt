// Test: External .dtt annotation files loaded via --dtt-annotations= plugin arg
// Verifies that preconditions and postconditions from .dtt files are applied.

// A custom function annotated only via the external .dtt file
void guarded_deref(int* p);

void test_custom_annotation_pass() {
    int x = 42;
    int* p = &x;
    guarded_deref(p);  // OK - pointer is non-null
}
