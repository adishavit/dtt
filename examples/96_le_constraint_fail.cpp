// Example 96: Less-than-or-equal violation — should FAIL
// Tests <= constraint with simple field vs constant

struct Config { int maxItems; int timeout; };

[[clang::annotate("dtt:requires=maxItems <= 100")]]
void configure(Config& c);

int main() {
    Config c;
    c.maxItems = 150;  // Too many!
    c.timeout = 30;
    configure(c);  // ERROR: 150 > 100
    
    return 0;
}
