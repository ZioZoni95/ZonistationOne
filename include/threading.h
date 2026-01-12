#ifndef THREADING_H
#define THREADING_H

/**
 * threading.h
 * 
 * Multi-threading primitives for ZonistationOne Emulator
 * Adapted from DuckStation's threading model but implemented in C using POSIX threads
 * 
 * Architecture Overview:
 * - CPU Thread: Main emulation thread (runs CPU, IRQs, Timers, DMA controllers)
 * - GPU Thread: Handles GPU commands, rendering, and display (optional, can be disabled)
 * - Audio Thread: Handles SPU audio generation (optional, SDL audio callback)
 * 
 * Synchronization:
 * - Command FIFO for CPU->GPU communication
 * - Semaphores/condition variables for thread wakeup
 * - Spin-wait optimization for low latency
 */

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <semaphore.h>

// ============================================================================
// Thread Handle
// ============================================================================

typedef struct ThreadHandle {
    pthread_t thread_id;
    bool is_valid;
    bool is_running;
    char name[64];
} ThreadHandle;

/**
 * @brief Create and start a new thread
 * @param handle Thread handle to initialize
 * @param name Thread name (for debugging)
 * @param func Thread entry point function
 * @param user_data User data passed to thread function
 * @return true on success, false on failure
 */
bool thread_create(ThreadHandle* handle, const char* name, 
                   void* (*func)(void*), void* user_data);

/**
 * @brief Wait for a thread to complete
 * @param handle Thread handle to wait for
 */
void thread_join(ThreadHandle* handle);

/**
 * @brief Detach a thread (no need to join)
 * @param handle Thread handle to detach
 */
void thread_detach(ThreadHandle* handle);

/**
 * @brief Set the name of the current thread (for debuggers)
 * @param name Thread name
 */
void thread_set_name(const char* name);

/**
 * @brief Yield the current thread's time slice
 */
void thread_yield(void);

/**
 * @brief Sleep for a number of microseconds
 * @param microseconds Time to sleep
 */
void thread_sleep_us(uint64_t microseconds);

// ============================================================================
// Mutex (Mutual Exclusion)
// ============================================================================

typedef struct Mutex {
    pthread_mutex_t mutex;
    bool is_initialized;
} Mutex;

/**
 * @brief Initialize a mutex
 * @param mutex Mutex to initialize
 */
void mutex_init(Mutex* mutex);

/**
 * @brief Destroy a mutex
 * @param mutex Mutex to destroy
 */
void mutex_destroy(Mutex* mutex);

/**
 * @brief Lock a mutex (blocking)
 * @param mutex Mutex to lock
 */
void mutex_lock(Mutex* mutex);

/**
 * @brief Try to lock a mutex (non-blocking)
 * @param mutex Mutex to try to lock
 * @return true if locked, false if already locked
 */
bool mutex_try_lock(Mutex* mutex);

/**
 * @brief Unlock a mutex
 * @param mutex Mutex to unlock
 */
void mutex_unlock(Mutex* mutex);

// ============================================================================
// Condition Variable (for thread signaling)
// ============================================================================

typedef struct ConditionVariable {
    pthread_cond_t cond;
    bool is_initialized;
} ConditionVariable;

/**
 * @brief Initialize a condition variable
 * @param cv Condition variable to initialize
 */
void condvar_init(ConditionVariable* cv);

/**
 * @brief Destroy a condition variable
 * @param cv Condition variable to destroy
 */
void condvar_destroy(ConditionVariable* cv);

/**
 * @brief Wait on a condition variable (releases mutex while waiting)
 * @param cv Condition variable to wait on
 * @param mutex Mutex that must be locked before calling
 */
void condvar_wait(ConditionVariable* cv, Mutex* mutex);

/**
 * @brief Wait on a condition variable with timeout
 * @param cv Condition variable to wait on
 * @param mutex Mutex that must be locked before calling
 * @param timeout_us Timeout in microseconds
 * @return true if signaled, false if timeout
 */
bool condvar_wait_timeout(ConditionVariable* cv, Mutex* mutex, uint64_t timeout_us);

/**
 * @brief Signal one thread waiting on a condition variable
 * @param cv Condition variable to signal
 */
void condvar_signal(ConditionVariable* cv);

/**
 * @brief Signal all threads waiting on a condition variable
 * @param cv Condition variable to broadcast to
 */
void condvar_broadcast(ConditionVariable* cv);

// ============================================================================
// Semaphore (counting semaphore for synchronization)
// ============================================================================

typedef struct Semaphore {
    sem_t sem;
    bool is_initialized;
} Semaphore;

/**
 * @brief Initialize a semaphore
 * @param semaphore Semaphore to initialize
 * @param initial_value Initial value for the semaphore
 */
void semaphore_init(Semaphore* semaphore, unsigned int initial_value);

/**
 * @brief Destroy a semaphore
 * @param semaphore Semaphore to destroy
 */
void semaphore_destroy(Semaphore* semaphore);

/**
 * @brief Wait (decrement) on a semaphore (blocking)
 * @param semaphore Semaphore to wait on
 */
void semaphore_wait(Semaphore* semaphore);

/**
 * @brief Try to wait on a semaphore (non-blocking)
 * @param semaphore Semaphore to try to wait on
 * @return true if decremented, false if would block
 */
bool semaphore_try_wait(Semaphore* semaphore);

/**
 * @brief Signal (increment) a semaphore
 * @param semaphore Semaphore to signal
 */
void semaphore_post(Semaphore* semaphore);

// ============================================================================
// Atomic Operations (lock-free primitives)
// ============================================================================

typedef struct AtomicInt32 {
    volatile int32_t value;
} AtomicInt32;

typedef struct AtomicUInt32 {
    volatile uint32_t value;
} AtomicUInt32;

/**
 * @brief Atomically load a 32-bit signed value
 * @param atomic Atomic variable to load
 * @return Current value
 */
int32_t atomic_load_i32(const AtomicInt32* atomic);

/**
 * @brief Atomically store a 32-bit signed value
 * @param atomic Atomic variable to store to
 * @param value Value to store
 */
void atomic_store_i32(AtomicInt32* atomic, int32_t value);

/**
 * @brief Atomically compare and exchange a 32-bit signed value
 * @param atomic Atomic variable to operate on
 * @param expected Expected current value (updated with actual value if failed)
 * @param desired New value to store if expected matches
 * @return true if exchange succeeded, false otherwise
 */
bool atomic_compare_exchange_i32(AtomicInt32* atomic, int32_t* expected, int32_t desired);

/**
 * @brief Atomically increment and return new value
 * @param atomic Atomic variable to increment
 * @return New value after increment
 */
int32_t atomic_fetch_add_i32(AtomicInt32* atomic, int32_t value);

// Similar functions for uint32_t
uint32_t atomic_load_u32(const AtomicUInt32* atomic);
void atomic_store_u32(AtomicUInt32* atomic, uint32_t value);
bool atomic_compare_exchange_u32(AtomicUInt32* atomic, uint32_t* expected, uint32_t desired);
uint32_t atomic_fetch_add_u32(AtomicUInt32* atomic, uint32_t value);

// ============================================================================
// Spin Lock (busy-wait lock for very short critical sections)
// ============================================================================

typedef struct SpinLock {
    AtomicInt32 lock;
} SpinLock;

/**
 * @brief Initialize a spin lock
 * @param spinlock Spin lock to initialize
 */
void spinlock_init(SpinLock* spinlock);

/**
 * @brief Acquire a spin lock (busy wait)
 * @param spinlock Spin lock to acquire
 */
void spinlock_lock(SpinLock* spinlock);

/**
 * @brief Try to acquire a spin lock (non-blocking)
 * @param spinlock Spin lock to try to acquire
 * @return true if acquired, false if already locked
 */
bool spinlock_try_lock(SpinLock* spinlock);

/**
 * @brief Release a spin lock
 * @param spinlock Spin lock to release
 */
void spinlock_unlock(SpinLock* spinlock);

// ============================================================================
// Timing Utilities
// ============================================================================

/**
 * @brief Get current time in nanoseconds (monotonic clock)
 * @return Current time in nanoseconds
 */
uint64_t time_get_nanos(void);

/**
 * @brief Get current time in microseconds (monotonic clock)
 * @return Current time in microseconds
 */
uint64_t time_get_micros(void);

/**
 * @brief Sleep until a specific time point (spin-wait if close)
 * @param target_nanos Target time in nanoseconds
 * @param use_spin Whether to spin-wait for the last few microseconds
 */
void time_sleep_until(uint64_t target_nanos, bool use_spin);

#endif // THREADING_H
