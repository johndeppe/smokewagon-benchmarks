#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>

#define PAGE_SIZE 4096
#define PAGES     8         // even number

int main(void) {
    char* ptr;
    int result;

    // allocate: https://man7.org/linux/man-pages/man3/malloc.3.html
    result = posix_memalign( (void**)&ptr, PAGE_SIZE, PAGES*PAGE_SIZE );
    if (result != 0) {
        printf("posix_memaligned() failed and returned: %d", result);
        return -1;
    }

    // touch first half:
    for (long i = 0; i < PAGES*PAGE_SIZE/2; i++ ) {
        ptr[i] = 'x';
    }
    printf("touched first half of allocated memory.\n");

    // madvise: https://man7.org/linux/man-pages/man2/madvise.2.html
    result = madvise(ptr, PAGES*PAGE_SIZE, 26); // 26 advises MADV_PRIVATE_TLB
    if (result != 0) {
        printf("madvise returned: %d, errno: %d, strerror: %s\n", result, errno, strerror(errno));
    }

    // touch second half:
    for (long i = PAGES*PAGE_SIZE/2; i < PAGES*PAGE_SIZE; i++ ) {
        ptr[i] = 'x';
    }
    printf("touched all allocated memory.\n");

    // free
    free(ptr);

    return 0;
}

/* Things to try:
 * 1. Clearing the madvise (MADV_NORMAL?)
 */
