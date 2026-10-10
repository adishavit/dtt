// An unannotated external initializer provides no proof of output state.
// Enforce mode must reject the later precondition check.

struct Buffer {
    int* data;
    int _size;
};

// Intended initializer, but its declaration alone does not establish a result.
// Proposed: [[clang::annotate("dtt:out:arg0=_size >= arg1")]]
void initBuffer(Buffer& buf, int count);

[[clang::annotate("dtt:requires=_size > 0")]]
void processBuffer(Buffer& buf);

int main() {
    Buffer b;
    // b._size is uninitialized/unknown

    initBuffer(b, 10);  // No postcondition is declared.

    // The call leaves the tracked property unknown.
    processBuffer(b);  // ERROR: initBuffer declares no output guarantee  dtt-error {{_size > 0}}

    return 0;
}
