// 1600: Adapter container pop() should decrement _size - DTT gap
// stack/queue/priority_queue pop() only checks nonEmpty precondition
// but does NOT decrement _size. After push+pop, _size remains 1 instead of 0,
// so subsequent top()/front() on empty container goes uncaught.

#include <stack>
#include <queue>

// Variant 1: stack push+pop+top - _size should be 0 after pop
void variant1_stack_pop_then_top() {
    std::stack<int> s;
    s.push(42);   // _size = 1
    s.pop();      // BUG: _size stays 1 (should be 0)
    s.top();      // ERROR: _size should be 0, access on empty
}

// Variant 2: queue push+pop+front - _size should be 0 after pop
void variant2_queue_pop_then_front() {
    std::queue<int> q;
    q.push(42);   // _size = 1
    q.pop();      // BUG: _size stays 1 (should be 0)
    q.front();    // ERROR: _size should be 0, access on empty
}

// Variant 3: priority_queue push+pop+top - _size should be 0 after pop
void variant3_pqueue_pop_then_top() {
    std::priority_queue<int> pq;
    pq.push(42);  // _size = 1
    pq.pop();     // BUG: _size stays 1 (should be 0)
    pq.top();     // ERROR: _size should be 0, access on empty
}

// Variant 4: multiple push+pop, net size 0
void variant4_stack_multi_pop() {
    std::stack<int> s;
    s.push(1);    // _size = 1
    s.push(2);    // _size = 2
    s.pop();      // _size should be 1
    s.pop();      // _size should be 0
    s.top();      // ERROR: _size should be 0
}

int main() { return 0; }
