#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdbool.h>

#define PAGE_SIZE   4096
#define PAGES       8         // even number
#define NUM_THREADS    64

void* test_smokewagon(void* tid) {
    char* ptr;
    int result;
    bool valid = true;

    // allocate: https://man7.org/linux/man-pages/man3/malloc.3.html
    result = posix_memalign( (void**)&ptr, PAGE_SIZE, PAGES*PAGE_SIZE );
    if (result != 0) {
        printf("tid %ld: posix_memaligned() failed and returned: %d\n", (long) tid, result);
        return (void*) -1;
    }

    // touch first half:
    for (long i = 0; i < PAGES*PAGE_SIZE/2; i++ ) {
        ptr[i] = 'x';
    }
    printf("tid %ld: touched first half of allocated memory.\n", (long) tid);

    // madvise: https://man7.org/linux/man-pages/man2/madvise.2.html
    result = madvise(ptr, PAGES*PAGE_SIZE, 26); // 26 advises MADV_PRIVATE_TLB
    if (result != 0) {
        printf("tid %ld: madvise returned: %d, errno: %d, strerror: %s\n", (long) tid, result, errno, strerror(errno));
    }

    // touch second half:
    for (long i = PAGES*PAGE_SIZE/2; i < PAGES*PAGE_SIZE; i++ ) {
        ptr[i] = 'x';
    }
    printf("tid %ld: touched all allocated memory.\n", (long) tid);

    // check correctness
    for (long i = 0; i < PAGES*PAGE_SIZE; i++) {
	if (ptr[i] != 'x') {
	    printf("tid %ld: oops, ptr[%ld] is '%c' instead of 'x'\n", (long) tid, i, ptr[i]);
	    valid = false;
	    }
    }

    // normalize
    result = madvise(ptr, PAGES*PAGE_SIZE, 27); // 27 advises MADV_NORMAL_TLB
    if (result != 0) {
	    printf("tid %ld: madvise returned: %d, errno: %d, strerror: %s\n", (long) tid, result, errno, strerror(errno));
    }

    // free
    free(ptr);
    if (valid == true) {
        return (void*) 0;
    } else {
        return (void*) -1;
    }
}

int main(void) {
    pthread_t threads[NUM_THREADS];
    int returns[NUM_THREADS];
    long ret;


    // create threads
    for (long i=0;i<NUM_THREADS;i++) {
        int ret = pthread_create(&threads[i], NULL, test_smokewagon, (void*) i);
        if (ret) printf("ERROR: return code for thread %ld from pthread_create() is %d\n", i, ret);
    }

    // join created threads
    for (long i=0;i<NUM_THREADS;i++) {
        int ret = pthread_join(threads[i], NULL);
        if (ret) printf("ERROR: return code %d from thread %ld\n", ret, i);
    }
}

/* Things to try:
 * 1. Not clearing the entire madvise.
 */

