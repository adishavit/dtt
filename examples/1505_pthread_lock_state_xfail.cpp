// 1505: pthread lock/unlock state tracking - DTT gap
// DTT can track init/destroy lifecycle pairs (resource tracking) but
// cannot track lock/unlock state transitions on the same resource.
// Real bugs: double-lock, unlock-without-lock, destroy-while-locked.

#include <pthread.h>

// Variant 1: Double lock on non-recursive mutex - deadlock
void variant1_double_lock() {
    pthread_mutex_t mtx;
    pthread_mutex_init(&mtx, nullptr);

    pthread_mutex_lock(&mtx);
    pthread_mutex_lock(&mtx);  // BUG: deadlock on non-recursive mutex

    pthread_mutex_unlock(&mtx);
    pthread_mutex_unlock(&mtx);
    pthread_mutex_destroy(&mtx);
}

// Variant 2: Unlock without lock
void variant2_unlock_without_lock() {
    pthread_mutex_t mtx;
    pthread_mutex_init(&mtx, nullptr);

    pthread_mutex_unlock(&mtx);  // BUG: unlock without preceding lock

    pthread_mutex_destroy(&mtx);
}

// Variant 3: Destroy while locked
void variant3_destroy_while_locked() {
    pthread_mutex_t mtx;
    pthread_mutex_init(&mtx, nullptr);

    pthread_mutex_lock(&mtx);
    pthread_mutex_destroy(&mtx);  // BUG: destroying a locked mutex is UB
}

int main() { return 0; }
