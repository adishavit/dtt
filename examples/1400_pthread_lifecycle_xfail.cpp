// 1400: Pthread resource lifecycle leaks - DTT gap
// DTT's scope-exit constraints only work on function return values, not
// out-parameters. pthread_mutex_init writes to arg0 (out-param), so DTT
// cannot enforce that @mutex == 0 at scope exit.

#include <cstdlib>
#include <pthread.h>

// Variant 1: mutex init without destroy - resource leak
void variant1_mutex_leak() {
    pthread_mutex_t mtx;
    pthread_mutex_init(&mtx, nullptr);  // Acquires @mutex
    // BUG: missing pthread_mutex_destroy - resource leak
}

// Variant 2: cond init without destroy - resource leak
void variant2_cond_leak() {
    pthread_cond_t cond;
    pthread_cond_init(&cond, nullptr);  // Acquires @cond
    // BUG: missing pthread_cond_destroy - resource leak
}

// Variant 3: rwlock init without destroy - resource leak
void variant3_rwlock_leak() {
    pthread_rwlock_t rwl;
    pthread_rwlock_init(&rwl, nullptr);  // Acquires @rwlock
    // BUG: missing pthread_rwlock_destroy - resource leak
}

int main() { return 0; }
