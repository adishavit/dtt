// Example 30: Array with guard — should COMPILE

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len > 0")]]
int head(Vec& v);

int main(int argc, char**) {
    Vec arr[3];
    arr[0].len = argc;  // Runtime
    
    if (arr[0].len > 0) {
        head(arr[0]);  // OK: guarded
    }
    
    return 0;
}
