// Example 29: Constant array index with bad value — should FAIL

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len > 0")]]
int head(Vec& v);

int main() {
    Vec arr[3];
    arr[0].len = 5;
    arr[1].len = 0;  // Zero!
    arr[2].len = 7;
    
    head(arr[1]);  // ERROR: arr[1].len = 0, not > 0  dtt-error {{len > 0}}
    
    return 0;
}
