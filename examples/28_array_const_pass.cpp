// Example 28: Constant array indices — should COMPILE

struct Vec { int* data; int len; };

[[clang::annotate("dtt:requires=len > 0")]]
int head(Vec& v);

int main() {
    Vec arr[3];
    arr[0].len = 5;
    arr[1].len = 3;
    arr[2].len = 7;
    
    head(arr[0]);  // OK: arr[0].len = 5 > 0
    head(arr[1]);  // OK: arr[1].len = 3 > 0
    head(arr[2]);  // OK: arr[2].len = 7 > 0
    
    return 0;
}
