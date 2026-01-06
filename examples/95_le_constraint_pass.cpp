// Example 95: Less-than-or-equal constraint — should COMPILE
// Tests <= constraint checking

struct Config { int maxItems; int timeout; };

[[clang::annotate("dtt:requires=maxItems <= 100")]]
void configure(Config& c);

int main() {
    Config c;
    c.maxItems = 50;  // OK: 50 <= 100
    c.timeout = 30;
    configure(c);
    
    Config c2;
    c2.maxItems = 100;  // OK: 100 <= 100
    c2.timeout = 60;
    configure(c2);
    
    return 0;
}
