// Example 5: Any property tracking (not just len)
struct Buffer {
    char* data;
    int size;
    int capacity;
};

[[clang::annotate("dtt:requires=size > 0")]]
char first(Buffer& b) { return b.data[0]; }

[[clang::annotate("dtt:requires=capacity >= 100")]]
void needsBigBuffer(Buffer& b) { (void)b; }

int main() {
    char storage[200];
    Buffer b;
    b.data = storage;
    b.size = 10;
    b.capacity = 200;
    
    first(b);          // OK: size=10 > 0
    needsBigBuffer(b); // OK: capacity=200 >= 100
    
    return 0;
}
