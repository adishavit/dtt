// Independent indices are not related to range iteration counts: the index
// grows with each iteration, so it is not bounded by the capacity.
// Both functions exceed the annotated capacity on the final iteration.
#include <initializer_list>

struct RangeBuffer {
    int _size;
    [[clang::annotate("dtt:requires=arg0 < _size")]]
    int& operator[](int index);
};

void initializer_list_overrun() {
    RangeBuffer buffer;
    buffer._size = 3;
    int index = 0;
    for (int value : {1, 2, 3, 4})
        buffer[index++] = value;  // dtt-error {{cannot verify|out of bounds}}
}

void array_range_overrun() {
    RangeBuffer buffer;
    buffer._size = 2;
    int values[] = {1, 2, 3};
    int index = 0;
    for (int value : values)
        buffer[index++] = value;  // dtt-error {{cannot verify|out of bounds}}
}
