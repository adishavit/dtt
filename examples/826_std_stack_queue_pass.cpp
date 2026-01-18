// 826_std_stack_queue_pass.cpp
// Tests: Safe std::stack/std::queue/std::priority_queue usage

#include <stack>
#include <queue>

// =============================================================================
// SECTION 1: std::stack - Guard with empty()
// =============================================================================

// Variant 1.1: if (!empty()) guard then top()
void test_stack_empty_guard() {
    std::stack<int> s;
    s.push(42);
    if (!s.empty()) {
        s.top();  // OK: guarded
    }
}

// Variant 1.2: negated empty guard
void test_stack_negated_empty() {
    std::stack<int> s;
    s.push(42);
    if (!s.empty()) {
        s.top();  // OK: guarded by !empty()
    }
}

// =============================================================================
// SECTION 2: std::stack - Access after push
// =============================================================================

// Variant 2.1: push then top
void test_stack_push_top() {
    std::stack<int> s;
    s.push(42);
    s.top();  // OK: just pushed, size >= 1
}

// Variant 2.2: emplace then top
void test_stack_emplace_top() {
    std::stack<int> s;
    s.emplace(42);
    s.top();  // OK: just emplaced
}

// Variant 2.3: multiple pushes
void test_stack_multiple_push() {
    std::stack<int> s;
    s.push(1);
    s.push(2);
    s.push(3);
    s.top();  // OK: size >= 3
}

// =============================================================================
// SECTION 3: std::stack - Guard with size()
// =============================================================================

// Variant 3.1: if (size() > 0) guard
void test_stack_size_guard() {
    std::stack<int> s;
    s.push(42);
    if (s.size() > 0) {
        s.top();  // OK: guarded
    }
}

// =============================================================================
// SECTION 4: std::stack - Pop with re-check
// =============================================================================

// Variant 4.1: push, pop, push, access
void test_stack_pop_push() {
    std::stack<int> s;
    s.push(1);
    s.pop();
    s.push(2);
    s.top();  // OK: pushed after pop
}

// Variant 4.2: pop with guard
void test_stack_pop_guard() {
    std::stack<int> s;
    s.push(1);
    s.push(2);
    s.pop();  // size now 1
    if (!s.empty()) {
        s.top();  // OK: guarded
    }
}

// =============================================================================
// SECTION 5: std::queue - Guard with empty()
// =============================================================================

// Variant 5.1: if (!empty()) guard then front()
void test_queue_empty_guard_front() {
    std::queue<int> q;
    q.push(42);
    if (!q.empty()) {
        q.front();  // OK: guarded
    }
}

// Variant 5.2: if (!empty()) guard then back()
void test_queue_empty_guard_back() {
    std::queue<int> q;
    q.push(42);
    if (!q.empty()) {
        q.back();  // OK: guarded
    }
}

// =============================================================================
// SECTION 6: std::queue - Access after push
// =============================================================================

// Variant 6.1: push then front/back
void test_queue_push() {
    std::queue<int> q;
    q.push(42);
    q.front();  // OK: just pushed
    q.back();   // OK
}

// Variant 6.2: emplace then access
void test_queue_emplace() {
    std::queue<int> q;
    q.emplace(42);
    q.front();  // OK: just emplaced
}

// =============================================================================
// SECTION 7: std::queue - Guard with size()
// =============================================================================

// Variant 7.1: if (size() > 0) guard
void test_queue_size_guard() {
    std::queue<int> q;
    q.push(42);
    if (q.size() > 0) {
        q.front();  // OK: guarded
    }
}

// =============================================================================
// SECTION 8: std::priority_queue - Guard with empty()
// =============================================================================

// Variant 8.1: if (!empty()) guard then top()
void test_pqueue_empty_guard() {
    std::priority_queue<int> pq;
    pq.push(42);
    if (!pq.empty()) {
        pq.top();  // OK: guarded
    }
}

// Variant 8.2: negated empty guard
void test_pqueue_negated_empty() {
    std::priority_queue<int> pq;
    pq.push(42);
    if (!pq.empty()) {
        pq.top();  // OK: guarded by !empty()
    }
}

// =============================================================================
// SECTION 9: std::priority_queue - Access after push
// =============================================================================

// Variant 9.1: push then top
void test_pqueue_push() {
    std::priority_queue<int> pq;
    pq.push(42);
    pq.top();  // OK: just pushed
}

// Variant 9.2: emplace then top
void test_pqueue_emplace() {
    std::priority_queue<int> pq;
    pq.emplace(42);
    pq.top();  // OK: just emplaced
}

// Variant 9.3: multiple pushes
void test_pqueue_multiple_push() {
    std::priority_queue<int> pq;
    pq.push(1);
    pq.push(5);
    pq.push(3);
    pq.top();  // OK: returns max (5)
}

// =============================================================================
// SECTION 10: std::priority_queue - Guard with size()
// =============================================================================

// Variant 10.1: if (size() > 0) guard
void test_pqueue_size_guard() {
    std::priority_queue<int> pq;
    pq.push(42);
    if (pq.size() > 0) {
        pq.top();  // OK: guarded
    }
}

// =============================================================================
// SECTION 11: Copy preserves elements
// =============================================================================

// Variant 11.1: copy stack
void test_stack_copy() {
    std::stack<int> s1;
    s1.push(42);
    std::stack<int> s2 = s1;
    s2.top();  // OK: copied from non-empty
}

// Variant 11.2: copy queue
void test_queue_copy() {
    std::queue<int> q1;
    q1.push(42);
    std::queue<int> q2 = q1;
    q2.front();  // OK: copied from non-empty
}

int main() {
    return 0;
}
