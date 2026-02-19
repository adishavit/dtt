// Test: External .dtt annotation files loaded via --dtt-annotations= plugin arg
// Verifies that preconditions from .dtt files produce errors when violated.

// A custom function annotated only via the external .dtt file
void guarded_deref(int* p);

void test_custom_annotation_fail() {
    int* p = nullptr;
    guarded_deref(p);  // ERROR: pointer is null, violates requires arg0._nonnull != 0
}
