#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdbool.h>

#define PAGE_SIZE   4096
#define PAGES       2         // even number
#define NUM_THREADS    1

#define MAP_PRIVATE_TLB	0x200000
#define MADV_NORMAL_TLB 27

void* test_smokewagon(void* tid) {
    char* ptr;
    int result;
    bool valid = true;
    char filename[50];

    printf("tid %ld: reporting in\n", (long) tid);

    // open file: 
    snprintf(filename, sizeof(filename), "exercise-filebacked-temp-%02ld", (long) tid);
    int fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if(fd == -1) {
        perror("file opening error!");
        return (void*)-2;
    }
    printf("tid %ld: opened file %s\n", (long) tid, filename);

    // extend file (by truncating lol): 
    result = ftruncate(fd, PAGES*PAGE_SIZE);
    if (result == -1) {
        perror("ftruncate");
        close(fd);
        return (void*)-3;
    }
    printf("tid %ld: truncated file to %d\n", (long) tid, PAGES*PAGE_SIZE);

    // allocate: https://man7.org/linux/man-pages/man3/malloc.3.html
    // mmap: https://man7.org/linux/man-pages/man2/mmap.2.html
    ptr = (void*)mmap(NULL, PAGES*PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE_TLB|MAP_SHARED, fd, 0);
    if ((void*)ptr == MAP_FAILED) {
        printf("tid %ld: mmap() failed and returned NULL\n", (long) tid);
        perror("mmap");
        return (void*)-4;
    }
    printf("tid %ld: mmap()'d\n", (long) tid);

    // touch first half:
    for (long i = 0; i < PAGES*PAGE_SIZE/2; i++ ) {
        ptr[i] = 'x';
    }
    printf("tid %ld: touched first half of allocated memory.\n", (long) tid);

    // normalize half with madvise: https://man7.org/linux/man-pages/man2/madvise.2.html
    result = madvise(ptr, PAGES*PAGE_SIZE/2, MADV_NORMAL_TLB);
    if (result != 0) {
        printf("tid %ld: madvise returned: %d, errno: %d, strerror: %s\n", (long) tid, result, errno, strerror(errno));
    }

    // touch second half:
    for (long i = PAGES*PAGE_SIZE/2; i < PAGES*PAGE_SIZE; i++ ) {
        ptr[i] = 'x';
    }
    printf("tid %ld: touched second half of allocated memory.\n", (long) tid);

    // check correctness
    for (long i = 0; i < PAGES*PAGE_SIZE/2; i++) {
	if (ptr[i] != 'x') {
	    printf("tid %ld: oops, ptr[%ld] is '%c' instead of 'x'\n", (long) tid, i, ptr[i]);
	    valid = false;
	    }
    }
    
    // unmap, close, and delete: https://man7.org/linux/man-pages/man3/remove.3.html
    munmap(ptr, PAGES*PAGE_SIZE);
    close(fd);
    remove(filename);
    return (void*) 0;
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

