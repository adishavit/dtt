// Example 32: Different indices don't interfere — should COMPILE

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len > 0")]]
int head(Vec& v);

int main() {
    Vec arr[3];
    arr[0].len = 5;
    arr[1].len = 0;  // arr[1] is zero, but we don't use it
    
    head(arr[0]);  // OK: arr[0].len = 5, arr[1] doesn't affect it
    
    return 0;
}
