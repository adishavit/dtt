// Example 6: Compound constraints (AND, OR)
struct Buffer {
    int size;
    int capacity;
};

[[clang::annotate("dtt:requires=size > 0 && capacity >= 10")]]
void needsBoth(Buffer& b) { (void)b; }

int main() {
    Buffer b;
    b.size = 5;
    b.capacity = 20;
    
    needsBoth(b);  // OK: both conditions met
    
    return 0;
}
