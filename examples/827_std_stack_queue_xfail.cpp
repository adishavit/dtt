// 827_std_stack_queue_xfail.cpp
// Tests: Unsafe std::stack/std::queue/std::priority_queue usage

#include <stack>
#include <queue>

// =============================================================================
// SECTION 1: std::stack - Access on empty
// =============================================================================

// Variant 1.1: top() on default-constructed
void test_stack_top_empty() {
    std::stack<int> s;
    s.top();  // ERROR: _size == 0
}

// Variant 1.2: top() after pop empties stack
void test_stack_pop_empty() {
    std::stack<int> s;
    s.push(42);
    s.pop();
    s.top();  // ERROR: _size == 0
}

// Variant 1.3: multiple pops then access
void test_stack_multiple_pop() {
    std::stack<int> s;
    s.push(1);
    s.push(2);
    s.pop();
    s.pop();
    s.top();  // ERROR: _size == 0
}

// =============================================================================
// SECTION 2: std::stack - Wrong branch
// =============================================================================

// Variant 2.1: access in else of !empty()
void test_stack_wrong_branch() {
    std::stack<int> s;
    if (!s.empty()) {
        // OK here
    } else {
        s.top();  // ERROR: stack is empty in else branch
    }
}

// =============================================================================
// SECTION 3: std::stack - Access on parameter
// =============================================================================

// Variant 3.1: top() on parameter stack
void test_stack_param(std::stack<int>& s) {
    s.top();  // ERROR: can't prove _size > 0
}

// =============================================================================
// SECTION 4: std::stack - Access moved-from
// =============================================================================

// Variant 4.1: top() on moved-from stack
void test_stack_moved_from() {
    std::stack<int> s1;
    s1.push(42);
    std::stack<int> s2 = std::move(s1);
    s1.top();  // ERROR: moved-from state
}

// =============================================================================
// SECTION 5: std::queue - Access on empty
// =============================================================================

// Variant 5.1: front() on default-constructed
void test_queue_front_empty() {
    std::queue<int> q;
    q.front();  // ERROR: _size == 0
}

// Variant 5.2: back() on default-constructed
void test_queue_back_empty() {
    std::queue<int> q;
    q.back();  // ERROR: _size == 0
}

// Variant 5.3: access after pop empties queue
void test_queue_pop_empty() {
    std::queue<int> q;
    q.push(42);
    q.pop();
    q.front();  // ERROR: _size == 0
}

// =============================================================================
// SECTION 6: std::queue - Wrong branch
// =============================================================================

// Variant 6.1: access in else of !empty()
void test_queue_wrong_branch() {
    std::queue<int> q;
    if (!q.empty()) {
        // OK here
    } else {
        q.front();  // ERROR: queue is empty in else branch
    }
}

// =============================================================================
// SECTION 7: std::queue - Access on parameter
// =============================================================================

// Variant 7.1: front() on parameter queue
void test_queue_param(std::queue<int>& q) {
    q.front();  // ERROR: can't prove _size > 0
}

// =============================================================================
// SECTION 8: std::queue - Access moved-from
// =============================================================================

// Variant 8.1: front() on moved-from queue
void test_queue_moved_from() {
    std::queue<int> q1;
    q1.push(42);
    std::queue<int> q2 = std::move(q1);
    q1.front();  // ERROR: moved-from state
}

// =============================================================================
// SECTION 9: std::priority_queue - Access on empty
// =============================================================================

// Variant 9.1: top() on default-constructed
void test_pqueue_top_empty() {
    std::priority_queue<int> pq;
    pq.top();  // ERROR: _size == 0
}

// Variant 9.2: access after pop empties queue
void test_pqueue_pop_empty() {
    std::priority_queue<int> pq;
    pq.push(42);
    pq.pop();
    pq.top();  // ERROR: _size == 0
}

// =============================================================================
// SECTION 10: std::priority_queue - Wrong branch
// =============================================================================

// Variant 10.1: access in else of !empty()
void test_pqueue_wrong_branch() {
    std::priority_queue<int> pq;
    if (!pq.empty()) {
        // OK here
    } else {
        pq.top();  // ERROR: empty in else branch
    }
}

// =============================================================================
// SECTION 11: std::priority_queue - Access on parameter
// =============================================================================

// Variant 11.1: top() on parameter
void test_pqueue_param(std::priority_queue<int>& pq) {
    pq.top();  // ERROR: can't prove _size > 0
}

// =============================================================================
// SECTION 12: std::priority_queue - Access moved-from
// =============================================================================

// Variant 12.1: top() on moved-from
void test_pqueue_moved_from() {
    std::priority_queue<int> pq1;
    pq1.push(42);
    std::priority_queue<int> pq2 = std::move(pq1);
    pq1.top();  // ERROR: moved-from state
}

int main() {
    return 0;
}
