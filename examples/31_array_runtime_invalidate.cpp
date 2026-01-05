// Example 31: Runtime index invalidates — should FAIL

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len > 0")]]
int head(Vec& v);

int getIndex();

int main() {
    Vec arr[3];
    arr[0].len = 5;
    arr[1].len = 3;
    
    int i = getIndex();
    arr[i].len = 0;  // Runtime index — invalidates all arr[*].len
    
    head(arr[0]);  // ERROR: arr[0].len unknown after runtime write
    
    return 0;
}
