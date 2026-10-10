// 715_uaf_member_ptr_fail.cpp
// Test: Member pointer lifetime tracking (d->buffer pattern)
// Expected: DTT errors for use-after-free of struct member pointers

#include <cstdlib>
#include <cstring>

struct Data {
    int value;
    char* buffer;
    int* numbers;
};

// Variant 1: Simple member pointer use-after-free
void test_member_simple_uaf() {
    Data* d = (Data*)malloc(sizeof(Data));
    if (!d) return;
    d->buffer = (char*)malloc(100);
    if (!d->buffer) { free(d); return; }

    free(d->buffer);
    d->buffer[0] = 'x';  // ERROR: use-after-free of d->buffer  dtt-error {{use-after-free}}

    free(d);
}

// Variant 2: Member pointer dereference after free
void test_member_deref_uaf() {
    Data* d = (Data*)malloc(sizeof(Data));
    if (!d) return;
    d->numbers = (int*)malloc(10 * sizeof(int));
    if (!d->numbers) { free(d); return; }

    free(d->numbers);
    int x = *d->numbers;  // ERROR: use-after-free of d->numbers  dtt-error {{use-after-free}}
    (void)x;

    free(d);
}

// Variant 3: strcpy to freed member buffer
void test_member_strcpy_uaf() {
    Data* d = (Data*)malloc(sizeof(Data));
    if (!d) return;
    d->buffer = (char*)malloc(100);
    if (!d->buffer) { free(d); return; }

    strcpy(d->buffer, "Hello");
    free(d->buffer);
    strcpy(d->buffer, "World");  // ERROR: use-after-free  dtt-error {{use-after-free}}

    free(d);
}

// Variant 4: Member array access after free
void test_member_array_uaf() {
    Data* d = (Data*)malloc(sizeof(Data));
    if (!d) return;
    d->numbers = (int*)malloc(10 * sizeof(int));
    if (!d->numbers) { free(d); return; }

    for (int i = 0; i < 10; i++) d->numbers[i] = i;
    free(d->numbers);
    int sum = d->numbers[0] + d->numbers[5];  // ERROR: use-after-free (both accesses)  dtt-error {{use-after-free}}
    (void)sum;

    free(d);
}

// Variant 5: Stack struct with heap member
void test_stack_struct_member_uaf() {
    Data d;
    d.buffer = (char*)malloc(100);
    if (!d.buffer) return;

    strcpy(d.buffer, "Hello");
    free(d.buffer);
    char c = d.buffer[0];  // ERROR: use-after-free  dtt-error {{use-after-free}}
    (void)c;
}

// Variant 6: Nested access through pointer
void test_nested_member_uaf() {
    Data* d = (Data*)malloc(sizeof(Data));
    if (!d) return;
    d->buffer = (char*)malloc(100);
    if (!d->buffer) { free(d); return; }

    free(d->buffer);
    size_t len = strlen(d->buffer);  // ERROR: use-after-free passed to strlen  dtt-error {{use-after-free}}
    (void)len;

    free(d);
}

int main() {
    return 0;
}
