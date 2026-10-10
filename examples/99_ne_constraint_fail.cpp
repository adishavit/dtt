// Example 99: Not-equal constraint violation — should FAIL
// Tests != constraint with equal value

struct Handle { int fd; int state; };

[[clang::annotate("dtt:requires=fd != -1")]]
void useHandle(Handle& h);

int main() {
    Handle h;
    h.fd = -1;  // Invalid handle
    h.state = 0;
    useHandle(h);  // ERROR: fd == -1  dtt-error {{fd != \-1}}
    
    return 0;
}
