#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>

#define PAGE_SIZE 4096
#define PAGES     8

int main(void) {
    char* ptr;
    int err;

    // allocate: https://man7.org/linux/man-pages/man3/malloc.3.html
    err = posix_memalign( (void**)&ptr, PAGE_SIZE, PAGES*PAGE_SIZE );
    if (err != 0) {
        printf("posix_memaligned() failed and returned: %d", err);
        return -1;
    }
    

    // madvise: https://man7.org/linux/man-pages/man2/madvise.2.html
    int result = madvise(ptr, PAGES*PAGE_SIZE, 26); // 26 advises MADV_PRIVATE_TLB
    if (result != 0) {
        printf("madvise returned: %d, errno: %d, strerror: %s\n", result, errno, strerror(errno));
    }
    

    // touch:
    for (long i = 0; i < PAGES*PAGE_SIZE; i++ ) {
        ptr[i] = 'x';
    }

    // free
    free(ptr);

    return 0;
}

/* Things to try:
 * 1. Allocate, madvise, touch
 * 2. Allocate, touch, madvise
 * 3. Allocate multiple pages, touch some, madvise others
 * 4. Various permutations of madvise and touching between allocating and freeing.
 * 5. Clearing the madvise (MADV_NORMAL?)
 */