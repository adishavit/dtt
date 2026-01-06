// Example 98: Not-equal constraint — should COMPILE
// Tests != constraint checking

struct Handle { int fd; int state; };

[[clang::annotate("dtt:requires=fd != -1")]]
void useHandle(Handle& h);

int main() {
    Handle h;
    h.fd = 5;
    h.state = 0;
    useHandle(h);  // OK: 5 != -1
    
    return 0;
}
