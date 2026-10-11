// Compile for x86_64-pc-windows-msvc: copying an uninitialized source still reads it.
void standard_uninitialized_source() {
    __builtin_va_list source;
    __builtin_va_list destination;
    __builtin_va_copy(destination, source); // dtt-error {{uninitialized pointer 'source'}}
}

void microsoft_uninitialized_source() {
    __builtin_ms_va_list source;
    __builtin_ms_va_list destination;
    __builtin_ms_va_copy(destination, source); // dtt-error {{uninitialized pointer 'source'}}
}

extern "C" void *malloc(__SIZE_TYPE__);
extern "C" void free(void *);

void standard_freed_source() {
    __builtin_va_list source = static_cast<char *>(malloc(32));
    free(source);
    __builtin_va_list destination;
    __builtin_va_copy(destination, source); // dtt-error {{use.after.free}}
}

void microsoft_freed_source() {
    __builtin_ms_va_list source = static_cast<char *>(malloc(32));
    free(source);
    __builtin_ms_va_list destination;
    __builtin_ms_va_copy(destination, source); // dtt-error {{use.after.free}}
}
