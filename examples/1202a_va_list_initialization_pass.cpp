// va_start and va_copy write their destination, including pointer-based va_list
// implementations. The destination need not be initialized before either call.
#include <cstdarg>

int start_then_read(int count, ...) {
    va_list args;
    va_start(args, count);
    int value = va_arg(args, int);
    va_end(args);
    return value;
}

int copy_then_read(int count, ...) {
    va_list args;
    va_start(args, count);
    va_list copy;
    va_copy(copy, args);
    int value = va_arg(copy, int);
    va_end(copy);
    va_end(args);
    return value;
}

void copy_twice(int count, ...) {
    va_list args;
    va_start(args, count);
    va_list first;
    va_copy(first, args);
    va_list second;
    va_copy(second, first);
    va_end(second);
    va_end(first);
    va_end(args);
}
