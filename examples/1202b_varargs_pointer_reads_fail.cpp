// Handling va_list initialization must preserve ordinary argument-read errors.
void consume_pointer(char *);
void consume_varargs(int, ...);

void ordinary_pointer_read() {
    char *pointer;
    consume_pointer(pointer); // dtt-error {{uninitialized pointer 'pointer'}}
}

void variadic_pointer_read() {
    char *pointer;
    consume_varargs(1, pointer); // dtt-error {{uninitialized pointer 'pointer'}}
}
