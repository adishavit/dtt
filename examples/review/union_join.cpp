union Value { int integer; double real; };

double example(bool branch) {
    Value value;
#if CASE == 0
    if (branch) value.integer = 1; else value.real = 2.0;
    return value.real; // Neither member is definitely active after the join.
#elif CASE == 1
    if (branch) value.real = 2.0; else value.integer = 1;
    return value.integer;
#elif CASE == 2
    if (branch) value.integer = 1; else value.integer = 2;
    return value.real; // Identical discriminants retain a proven violation.
#elif CASE == 3
    if (branch) value.real = 1.0; else value.real = 2.0;
    return value.real;
#endif
}
