// Minimal contract examples verified for the talk-prep handoff.
#include <vector>
#include <utility>
#ifndef CASE
#define CASE 0
#endif
#if CASE == 0
struct SecureData {
  [[clang::annotate("dtt:ensures=@encrypted == 0")]]
  SecureData() {}
  [[clang::annotate("dtt:ensures=@encrypted == 1")]]
  void encrypt() {}
  [[clang::annotate("dtt:requires=@encrypted == 1")]]
  void send() {}
};
void demo() { SecureData secret; secret.send(); }
#elif CASE == 1
[[clang::annotate("dtt:requires=_size >= 0")]]
void demo(std::vector<int>& v) { (void)v.front(); }
#elif CASE == 2
[[clang::annotate("dtt:requires=_size >= 0")]]
void demo(std::vector<int>& v) { if (!v.empty()) (void)v.front(); }
#elif CASE == 3
struct Connection {
  int _state = 0;
  [[clang::annotate("dtt:requires=_state == 0")]]
  [[clang::annotate("dtt:ensures=_state == 1")]] void connect();
  [[clang::annotate("dtt:requires=_state == 1")]]
  [[clang::annotate("dtt:ensures=_state == 2")]] void authenticate();
  [[clang::annotate("dtt:requires=_state == 2")]] void send(const char*);
};
void demo() { Connection c; c.send("x"); c.authenticate(); c.connect(); }
#elif CASE == 4
void consume(std::vector<int>);
void demo() { std::vector<int> v = {1,2,3}; (void)v[0]; consume(std::move(v)); (void)v[0]; }
#endif
