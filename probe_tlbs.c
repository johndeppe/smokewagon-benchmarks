#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#define PAGE_SIZE 4096
#define NUM_THREADS 64
#define NUM_BARRIERS NUM_THREADS + 1
#define PAGES_PER_THREAD 5
#define MADV_PROBE_TLB 28

pthread_t threads[NUM_THREADS];
pthread_barrier_t barriers[NUM_BARRIERS];
char* ptrs[NUM_THREADS];

void* probe_tlb_test(void* tid) {
    unsigned long cpu = (unsigned long) tid;
    int result;
    cpu_set_t cpuset;

    // pin thread to core
    CPU_ZERO(&cpuset);
    CPU_SET( cpu, &cpuset);
    result = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (result) {
        printf("pthread_setaffinity_np() returned %d\n", result);
        return (void*)(long) result;
    } else {
	; //printf("cpu %2lu pinned\n", cpu);
    }

    // allocate and touch pages, which loads TLB
    result = posix_memalign( (void**) &ptrs[cpu], PAGE_SIZE, PAGE_SIZE );
    if (result) {
        printf("cpu %2lu posix_memaligned() failed and returned: %d\n", cpu, result);
        return (void*)(long) result;
    } else {
	//printf("cpu %2lu has vpn 0x%lx\n", cpu, (unsigned long) ptrs[cpu] >> 12);
    }
    madvise(ptrs[cpu], PAGE_SIZE, 26); // smokewagon
    *(ptrs[cpu]) = 'x'; // touch page to fault-in and load TLB

    // check TLB for everyone's threads
    for (long i=0; i<NUM_THREADS; i++) {
        pthread_barrier_wait(&barriers[i]);

        // each thread/core probes TLB for core N's page and reports if they found it
        result = madvise(ptrs[i], PAGE_SIZE, MADV_PROBE_TLB);
        if (!result) { // TLB hit
            printf("cpu %2ld found cpu %2ld's page with vpn %p and returned %d\n", cpu, i, (unsigned long) ptrs[cpu] >> 12, result);
        } else {
	    // TLB miss
            // printf("cpu %2ld probed %p and returned %d\n", cpu, (unsigned long) ptrs[cpu] >> 12, result);
        }
    }
    pthread_barrier_wait(&barriers[NUM_THREADS]);
}

int main(void) {
    int result;

    // initialize barriers
    for (unsigned long i=0; i<NUM_BARRIERS; i++) {
        result = pthread_barrier_init(&barriers[i], NULL, NUM_THREADS);
        if (result) {
            printf("pthread_barrier_init() %ld failed with return code, %d\n", i, result);
            return result;
        }
    }
    // create a thread for each core
    for (unsigned long i=0; i<NUM_THREADS-1; i++) {
        result = pthread_create(&threads[i], NULL, probe_tlb_test, (void*) i);
        if (result) {
            printf("ERROR: return code for thread %ld from pthread_create() is %d\n", i, result);
            return result;
        }
    }

    probe_tlb_test( (void*) NUM_THREADS-1 );
}
