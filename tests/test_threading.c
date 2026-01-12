/**
 * test_threading.c
 * 
 * Unit tests for the threading system
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include "threading.h"

// Test counter for thread synchronization
static AtomicInt32 test_counter = {0};
static Mutex test_mutex;
static int shared_value = 0;

// Simple thread function
static void* test_thread_func(void* arg) {
    int thread_id = *(int*)arg;
    
    printf("[Thread %d] Started\n", thread_id);
    
    // Test atomic increment
    for (int i = 0; i < 1000; i++) {
        atomic_fetch_add_i32(&test_counter, 1);
    }
    
    // Test mutex
    for (int i = 0; i < 100; i++) {
        mutex_lock(&test_mutex);
        shared_value++;
        mutex_unlock(&test_mutex);
    }
    
    printf("[Thread %d] Finished\n", thread_id);
    return NULL;
}

// Test semaphore
static Semaphore test_sem;
static int sem_counter = 0;

static void* producer_thread(void* arg) {
    (void)arg;
    printf("[Producer] Starting\n");
    
    for (int i = 0; i < 5; i++) {
        thread_sleep_us(100000); // 100ms
        sem_counter++;
        printf("[Producer] Produced item %d\n", i + 1);
        semaphore_post(&test_sem);
    }
    
    printf("[Producer] Done\n");
    return NULL;
}

static void* consumer_thread(void* arg) {
    (void)arg;
    printf("[Consumer] Starting\n");
    
    for (int i = 0; i < 5; i++) {
        printf("[Consumer] Waiting for item...\n");
        semaphore_wait(&test_sem);
        printf("[Consumer] Consumed item %d\n", i + 1);
    }
    
    printf("[Consumer] Done\n");
    return NULL;
}

int main() {
    printf("=== Threading System Tests ===\n\n");
    
    // Test 1: Basic thread creation and join
    printf("Test 1: Thread creation and join\n");
    ThreadHandle handle;
    int thread_arg = 1;
    
    if (!thread_create(&handle, "TestThread", test_thread_func, &thread_arg)) {
        printf("FAILED: Could not create thread\n");
        return 1;
    }
    
    thread_join(&handle);
    printf("PASSED: Thread created and joined successfully\n\n");
    
    // Test 2: Multiple threads with atomics and mutex
    printf("Test 2: Multiple threads with atomics and mutex\n");
    atomic_store_i32(&test_counter, 0);
    shared_value = 0;
    mutex_init(&test_mutex);
    
    const int NUM_THREADS = 4;
    ThreadHandle threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];
    
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_ids[i] = i + 1;
        if (!thread_create(&threads[i], "Worker", test_thread_func, &thread_ids[i])) {
            printf("FAILED: Could not create thread %d\n", i);
            return 1;
        }
    }
    
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_join(&threads[i]);
    }
    
    int final_atomic = atomic_load_i32(&test_counter);
    printf("Atomic counter: %d (expected 4000)\n", final_atomic);
    printf("Shared value: %d (expected 400)\n", shared_value);
    
    if (final_atomic == 4000 && shared_value == 400) {
        printf("PASSED: Atomic operations and mutex work correctly\n\n");
    } else {
        printf("FAILED: Race condition detected!\n\n");
        return 1;
    }
    
    mutex_destroy(&test_mutex);
    
    // Test 3: Semaphore (producer-consumer)
    printf("Test 3: Semaphore (producer-consumer)\n");
    semaphore_init(&test_sem, 0);
    
    ThreadHandle producer, consumer;
    if (!thread_create(&producer, "Producer", producer_thread, NULL) ||
        !thread_create(&consumer, "Consumer", consumer_thread, NULL)) {
        printf("FAILED: Could not create producer/consumer threads\n");
        return 1;
    }
    
    thread_join(&consumer);
    thread_join(&producer);
    
    printf("PASSED: Semaphore synchronization works\n\n");
    semaphore_destroy(&test_sem);
    
    // Test 4: Timing utilities
    printf("Test 4: Timing utilities\n");
    uint64_t start = time_get_micros();
    thread_sleep_us(100000); // Sleep 100ms
    uint64_t end = time_get_micros();
    uint64_t elapsed = end - start;
    
    printf("Sleep test: %lu microseconds (expected ~100000)\n", elapsed);
    if (elapsed >= 95000 && elapsed <= 110000) {
        printf("PASSED: Timing utilities work\n\n");
    } else {
        printf("WARNING: Timing may be inaccurate\n\n");
    }
    
    // Test 5: Spin lock
    printf("Test 5: Spin lock\n");
    SpinLock spinlock;
    spinlock_init(&spinlock);
    
    spinlock_lock(&spinlock);
    printf("Acquired spinlock\n");
    
    if (!spinlock_try_lock(&spinlock)) {
        printf("Correctly failed to re-acquire locked spinlock\n");
    } else {
        printf("FAILED: Should not be able to re-acquire spinlock\n");
        return 1;
    }
    
    spinlock_unlock(&spinlock);
    
    if (spinlock_try_lock(&spinlock)) {
        printf("Successfully acquired unlocked spinlock\n");
        spinlock_unlock(&spinlock);
        printf("PASSED: Spin lock works\n\n");
    } else {
        printf("FAILED: Could not acquire unlocked spinlock\n");
        return 1;
    }
    
    printf("=== All Threading Tests Passed! ===\n");
    return 0;
}
