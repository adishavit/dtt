// 1400: Correct pthread resource lifecycle

#include <cstdlib>
#include <pthread.h>

// Variant 1: mutex init + destroy
void variant1_mutex_lifecycle() {
    pthread_mutex_t mtx;
    pthread_mutex_init(&mtx, nullptr);
    pthread_mutex_lock(&mtx);
    pthread_mutex_unlock(&mtx);
    pthread_mutex_destroy(&mtx);
}

// Variant 2: cond init + destroy
void variant2_cond_lifecycle() {
    pthread_cond_t cond;
    pthread_cond_init(&cond, nullptr);
    pthread_cond_destroy(&cond);
}

// Variant 3: rwlock init + destroy
void variant3_rwlock_lifecycle() {
    pthread_rwlock_t rwl;
    pthread_rwlock_init(&rwl, nullptr);
    pthread_rwlock_destroy(&rwl);
}

int main() { return 0; }
