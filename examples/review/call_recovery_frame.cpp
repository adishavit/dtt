// Failed read-only contracts must not destroy facts used by later valid code.
// Contracts declare modeled effects; failed mutators still cannot promise success.
#include <optional>
#include <vector>
#include <utility>

struct Record {
    int _state = 0;
    int len = 10;
    [[clang::annotate("dtt:requires=arg0 >= 20")]] void needsTwenty(int);
    [[clang::annotate("dtt:requires=_state == 0")]] void useZero();
    [[clang::annotate("dtt:requires=_state == 0")]]
    [[clang::annotate("dtt:ensures=_state = 1")]] void connect();
    [[clang::annotate("dtt:requires=_state == 1")]]
    [[clang::annotate("dtt:ensures=_state = 2")]] void authenticate();
    [[clang::annotate("dtt:requires=_state == 2")]] void send();
    [[clang::annotate("dtt:requires=arg1 >= 20")]] void query(Record&, int);
};
[[clang::annotate("dtt:requires=arg1 >= 20")]] void query(Record&, int);
[[clang::annotate("dtt:requires=arg1 >= 20")]] void consume(std::vector<int>, int);
int getValue();
void externalQuery(Record*, int);
[[clang::annotate("dtt:requires=arg2 >= 20")]]
[[clang::annotate("dtt:ensures=arg0._state = 2")]]
void mutatePair(Record&, Record&, int);
void externalMutatePair(Record*, Record*, int);

void test(int n) {
#if CASE == 0
    std::vector<int> v;
    (void)v.front(); // contract-error
    v.push_back(1);
    (void)v.front();
    (void)v[0];
    v.clear();
    (void)v.back(); // contract-error
    v.push_back(2);
    (void)v.back();
#elif CASE == 1
    std::vector<int> v = {1, 2};
    (void)v[2]; // contract-error
    (void)v[0];
    (void)v.at(2); // contract-error
    (void)v[1];
#elif CASE == 2
    std::optional<int> value;
    (void)value.value(); // contract-error
    value = 7;
    (void)value.value();
    value.reset();
    (void)*value; // contract-error
    value.emplace(8);
    (void)*value;
#elif CASE == 3
    Record r;
    r.len = getValue();
    r.needsTwenty(r.len); // contract-unknown
    r.useZero();
    if (r.len >= 20) r.needsTwenty(r.len);
#elif CASE == 4
    Record r;
    r.send(); // contract-error
    r.connect();
    r.authenticate();
    r.send();
#elif CASE == 5
    Record r;
    query(r, 0); // contract-error
    r.useZero();
    query(r, n); // contract-unknown
    r.useZero();
#elif CASE == 6
    Record r, other;
    r.query(other, n); // contract-unknown
    r.useZero();
    other.useZero();
#elif CASE == 7
    Record r;
    externalQuery(&r, 0); // contract-error
    r.useZero();
    externalQuery(&r, n); // contract-unknown
    r.useZero();
#elif CASE == 8
    std::vector<int> v = {1};
    consume(std::move(v), 0); // contract-error
    (void)v[0]; // contract-unknown
#elif CASE == 9
    Record r;
    r.len = 20;
    r.needsTwenty(0); // contract-error
    r.needsTwenty(r.len);
    r.useZero();
#elif CASE == 10
    Record r;
    r.authenticate(); // contract-error
    r.useZero(); // contract-unknown
#elif CASE == 11
    Record r;
    r.send(); // contract-error
    r.send(); // contract-error
    r.authenticate(); // contract-error
#elif CASE == 12
    Record r, other;
    mutatePair(r, other, 0); // contract-error
    r.useZero(); // contract-unknown
    other.useZero(); // contract-unknown
#elif CASE == 13
    Record r, other;
    externalMutatePair(&r, &other, 0); // contract-error
    r.useZero(); // contract-unknown
    other.useZero(); // contract-unknown
#endif
}
