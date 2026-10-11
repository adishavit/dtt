// Compile for x86_64-pc-windows-msvc: pointer-based va_list, no SDK required.
int standard_start_copy(int count, ...) {
    __builtin_va_list args;
    __builtin_va_start(args, count);
    __builtin_va_list copy;
    __builtin_va_copy(copy, args);
    int value = __builtin_va_arg(copy, int);
    __builtin_va_end(copy);
    __builtin_va_end(args);
    return value;
}

int microsoft_start_copy(int count, ...) {
    __builtin_ms_va_list args;
    __builtin_ms_va_start(args, count);
    __builtin_ms_va_list copy;
    __builtin_ms_va_copy(copy, args);
    int value = __builtin_va_arg(copy, int);
    __builtin_ms_va_end(copy);
    __builtin_ms_va_end(args);
    return value;
}
