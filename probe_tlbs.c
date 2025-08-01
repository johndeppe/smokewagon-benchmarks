#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#define PAGE_SIZE 4096
#define NUM_THREADS 64
#define NUM_BARRIERS NUM_THREADS + 1

pthread_t threads[NUM_THREADS];
pthread_barrier_t barriers[NUM_BARRIERS];
char* ptrs[NUM_THREADS];

void* probe_tlb_test(void* tid) {
    char* ptr;
    int result;
    cpu_set_t cpuset;
    pthread_t thread = pthread_self();
    printf("tid: %ld running\n", (long) tid);

    // pin thread to core
    CPU_ZERO(&cpuset);
    CPU_SET( (long) tid, &cpuset);
    result = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (result) {
        printf("pthread_setaffinity_np() returned %d\n", result);
        return (void*)(long) result;
    } /* else {
        printf("tid: %ld pinned\n", (long) tid);
    } */

    // allocate page and touch it, loading into TLB
    result = posix_memalign( (void**)&ptr, PAGE_SIZE, PAGE_SIZE );
    if (result) {
        printf("tid %ld: posix_memaligned() failed and returned: %d\n", (long) tid, result);
        return (void*)(long) result;
    }
    ptr[0] = 'x'; // touch page to fault-in and load TLB
    ptrs[(long) tid] = ptr; // share pointer with other threads

    for (long i=0; i<NUM_THREADS; i++) {

        // barrier
        // each thread/core probes TLB for core N's page and reports if they found it
        result = madvise(ptr, PAGE_SIZE, 28); // 28 advises MADV_PROBE_TLB
        if (result) {
            // found it
        } else {
            // didn't find it
        }
    }
}

int main(void) {
    int results[NUM_THREADS];
    int result;

    // initialize barriers
    for (long i=0; i<NUM_BARRIERS; i++) {
        result = pthread_barrier_init(&barriers[i], NULL, NUM_THREADS+1);
        if (result) {
            printf("pthread_barrier_init() %d failed with return code, %d\n", i, result);
            return result;
        } /* else {
            printf("pthread_barrier_init() %d initialized\n", i);
        } */
    }
    // create a thread for each core
    for (long i=0; i<NUM_THREADS; i++) {
        result = pthread_create(&threads[i], NULL, probe_tlb_test, (void*) i);
        if (result) {
            printf("ERROR: return code for thread %ld from pthread_create() is %d\n", i, result);
            return result;
        }
    }

    // get page in one core's TLB
//    ptr[0] = 'x';
    //printf("touched page.\n");

    // barrier
    // probe all cores & report with new madvise
    // barrier
}