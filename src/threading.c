/**
 * threading.c
 * 
 * Multi-threading primitives implementation for ZonistationOne Emulator
 * POSIX threads (pthread) implementation for Linux
 */

#define _GNU_SOURCE  // For pthread extensions on Linux
#include "threading.h"
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>

// ============================================================================
// Thread Handle Implementation
// ============================================================================

bool thread_create(ThreadHandle* handle, const char* name, 
                   void* (*func)(void*), void* user_data) {
    memset(handle, 0, sizeof(ThreadHandle));
    strncpy(handle->name, name, sizeof(handle->name) - 1);
    
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    
    int result = pthread_create(&handle->thread_id, &attr, func, user_data);
    pthread_attr_destroy(&attr);
    
    if (result == 0) {
        handle->is_valid = true;
        handle->is_running = true;
        return true;
    }
    
    return false;
}

void thread_join(ThreadHandle* handle) {
    if (handle->is_valid && handle->is_running) {
        pthread_join(handle->thread_id, NULL);
        handle->is_running = false;
    }
}

void thread_detach(ThreadHandle* handle) {
    if (handle->is_valid && handle->is_running) {
        pthread_detach(handle->thread_id);
        handle->is_running = false;
    }
}

void thread_set_name(const char* name) {
    #ifdef __linux__
    pthread_setname_np(pthread_self(), name);
    #endif
}

void thread_yield(void) {
    sched_yield();
}

void thread_sleep_us(uint64_t microseconds) {
    struct timespec ts;
    ts.tv_sec = microseconds / 1000000;
    ts.tv_nsec = (microseconds % 1000000) * 1000;
    nanosleep(&ts, NULL);
}

// ============================================================================
// Mutex Implementation
// ============================================================================

void mutex_init(Mutex* mutex) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
#ifdef __linux__
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
#else
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
#endif
    
    pthread_mutex_init(&mutex->mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    
    mutex->is_initialized = true;
}

void mutex_destroy(Mutex* mutex) {
    if (mutex->is_initialized) {
        pthread_mutex_destroy(&mutex->mutex);
        mutex->is_initialized = false;
    }
}

void mutex_lock(Mutex* mutex) {
    pthread_mutex_lock(&mutex->mutex);
}

bool mutex_try_lock(Mutex* mutex) {
    return pthread_mutex_trylock(&mutex->mutex) == 0;
}

void mutex_unlock(Mutex* mutex) {
    pthread_mutex_unlock(&mutex->mutex);
}

// ============================================================================
// Condition Variable Implementation
// ============================================================================

void condvar_init(ConditionVariable* cv) {
    pthread_cond_init(&cv->cond, NULL);
    cv->is_initialized = true;
}

void condvar_destroy(ConditionVariable* cv) {
    if (cv->is_initialized) {
        pthread_cond_destroy(&cv->cond);
        cv->is_initialized = false;
    }
}

void condvar_wait(ConditionVariable* cv, Mutex* mutex) {
    pthread_cond_wait(&cv->cond, &mutex->mutex);
}

bool condvar_wait_timeout(ConditionVariable* cv, Mutex* mutex, uint64_t timeout_us) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    
    // Add timeout
    ts.tv_sec += timeout_us / 1000000;
    ts.tv_nsec += (timeout_us % 1000000) * 1000;
    
    // Handle nanosecond overflow
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }
    
    int result = pthread_cond_timedwait(&cv->cond, &mutex->mutex, &ts);
    return result == 0; // true if signaled, false if timeout
}

void condvar_signal(ConditionVariable* cv) {
    pthread_cond_signal(&cv->cond);
}

void condvar_broadcast(ConditionVariable* cv) {
    pthread_cond_broadcast(&cv->cond);
}

// ============================================================================
// Semaphore Implementation
// ============================================================================

void semaphore_init(Semaphore* semaphore, unsigned int initial_value) {
    sem_init(&semaphore->sem, 0, initial_value);
    semaphore->is_initialized = true;
}

void semaphore_destroy(Semaphore* semaphore) {
    if (semaphore->is_initialized) {
        sem_destroy(&semaphore->sem);
        semaphore->is_initialized = false;
    }
}

void semaphore_wait(Semaphore* semaphore) {
    sem_wait(&semaphore->sem);
}

bool semaphore_try_wait(Semaphore* semaphore) {
    return sem_trywait(&semaphore->sem) == 0;
}

void semaphore_post(Semaphore* semaphore) {
    sem_post(&semaphore->sem);
}

// ============================================================================
// Atomic Operations (using C11 atomics)
// ============================================================================

int32_t atomic_load_i32(const AtomicInt32* atomic) {
    return __atomic_load_n(&atomic->value, __ATOMIC_ACQUIRE);
}

void atomic_store_i32(AtomicInt32* atomic, int32_t value) {
    __atomic_store_n(&atomic->value, value, __ATOMIC_RELEASE);
}

bool atomic_compare_exchange_i32(AtomicInt32* atomic, int32_t* expected, int32_t desired) {
    return __atomic_compare_exchange_n(&atomic->value, expected, desired,
                                       false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

int32_t atomic_fetch_add_i32(AtomicInt32* atomic, int32_t value) {
    return __atomic_fetch_add(&atomic->value, value, __ATOMIC_ACQ_REL);
}

uint32_t atomic_load_u32(const AtomicUInt32* atomic) {
    return __atomic_load_n(&atomic->value, __ATOMIC_ACQUIRE);
}

void atomic_store_u32(AtomicUInt32* atomic, uint32_t value) {
    __atomic_store_n(&atomic->value, value, __ATOMIC_RELEASE);
}

bool atomic_compare_exchange_u32(AtomicUInt32* atomic, uint32_t* expected, uint32_t desired) {
    return __atomic_compare_exchange_n(&atomic->value, expected, desired,
                                       false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

uint32_t atomic_fetch_add_u32(AtomicUInt32* atomic, uint32_t value) {
    return __atomic_fetch_add(&atomic->value, value, __ATOMIC_ACQ_REL);
}

// ============================================================================
// Spin Lock Implementation
// ============================================================================

void spinlock_init(SpinLock* spinlock) {
    atomic_store_i32(&spinlock->lock, 0);
}

void spinlock_lock(SpinLock* spinlock) {
    int32_t expected = 0;
    while (!atomic_compare_exchange_i32(&spinlock->lock, &expected, 1)) {
        expected = 0;
        // Pause/yield to reduce CPU contention
        __builtin_ia32_pause();
    }
}

bool spinlock_try_lock(SpinLock* spinlock) {
    int32_t expected = 0;
    return atomic_compare_exchange_i32(&spinlock->lock, &expected, 1);
}

void spinlock_unlock(SpinLock* spinlock) {
    atomic_store_i32(&spinlock->lock, 0);
}

// ============================================================================
// Timing Utilities
// ============================================================================

uint64_t time_get_nanos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

uint64_t time_get_micros(void) {
    return time_get_nanos() / 1000;
}

void time_sleep_until(uint64_t target_nanos, bool use_spin) {
    uint64_t now = time_get_nanos();
    
    if (now >= target_nanos) {
        return; // Already past target
    }
    
    uint64_t remaining = target_nanos - now;
    
    if (use_spin) {
        // Use OS sleep for most of the time, then spin for precision
        const uint64_t SPIN_THRESHOLD_NS = 2000000; // 2ms
        
        if (remaining > SPIN_THRESHOLD_NS) {
            // Sleep for most of the duration
            uint64_t sleep_duration = remaining - SPIN_THRESHOLD_NS;
            struct timespec ts;
            ts.tv_sec = sleep_duration / 1000000000;
            ts.tv_nsec = sleep_duration % 1000000000;
            nanosleep(&ts, NULL);
        }
        
        // Spin for the remaining time
        while (time_get_nanos() < target_nanos) {
            __builtin_ia32_pause();
        }
    } else {
        // Just use OS sleep
        struct timespec ts;
        ts.tv_sec = remaining / 1000000000;
        ts.tv_nsec = remaining % 1000000000;
        nanosleep(&ts, NULL);
    }
}
