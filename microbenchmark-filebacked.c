/* microbenchmark-fileback.c - each thread mmap-reads-munmaps a file */

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>      // for open()
#include <pthread.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>     // getopt guide: https://azrael.digipen.edu/~mmead/www/mg/getopt/index.html
#include <ctype.h>      // for isdigit()
#include <sched.h>
#include <sys/utsname.h> // for uname syscall
#include <stdint.h> // for uintptr_t

#define ONE_GB_SIZE (1ULL << 30)
#define HUGEPAGE_SIZE 2097152
#define PAGE_SIZE   4096
#define MAX_THREADS 64
#define FILENAME "microbenchmark-filebacked"
#define MAP_PRIVATE_TLB 0x200000

struct __attribute__ ((aligned (64))) per_thread_info {
    int tid;
    pthread_t thread;
    pthread_attr_t attr;
    cpu_set_t cpuset;
    unsigned long counter;
    int return_value;
    char* my_page;
    char* bystander_page;
    int fd;
};

long min_threads = 1;
long threads = 4;
long duration = 5;
long end;

int protread;
int protwrite;
int mmap_flags = MAP_SHARED|MAP_FIXED_NOREPLACE;
bool smokewagon = false; // smokewagon == false means don't use smokewagon, smokewagon == true means use smokewagon

void* test_smokewagon(void* info_ptr) {
    struct per_thread_info* my_info = info_ptr;
    int tid = my_info->tid;
    unsigned long local_counter = 0;
    struct timespec now;

    do {
        // mmap the thread's page in the file
        char* ptr = mmap(my_info->my_page, PAGE_SIZE, PROT_READ, mmap_flags, my_info->fd, 0);
        if (ptr == NULL) {
            printf("mmap() for tid: %d failed, ptr == NULL\n", tid);
            return info_ptr;
        } else if (ptr == MAP_FAILED) {
            printf("mmap() for tid: %d failed, ptr == MAP_FAILED\n", tid);
            return info_ptr;
        } else if (ptr != my_info->my_page) {
            printf("mmap() for tid: %d problem, ptr != my_info->my_page\n", tid);
            return info_ptr;
        }

        // read
        if (ptr[0] != 'y') {
            printf("uhoh, tid: %d misplaced its page somehow\n", tid);
        }

        // munmap page
        munmap(ptr, PAGE_SIZE);

        local_counter++;

        // check time
        clock_gettime(CLOCK_MONOTONIC, &now);
    } while ( end > now.tv_sec * 1000000000L + now.tv_nsec );

    my_info->counter = local_counter;

    return info_ptr;
}

int main(int argc, char *argv[]) {
    struct per_thread_info thread_infos[MAX_THREADS];
    long results[MAX_THREADS] = {0};

    // check opts
    int opt;
    char* endptr;
    while ((opt = getopt(argc, argv, "st:d:m:")) != -1) {
        switch(opt) {
            case 't':
                for (char *p = optarg; *p; p++) {
                    if (!isdigit(*p)) {
                        printf("Error: -t requires a positive integer\n");
                        return EXIT_FAILURE;
                    }
                }
                int opt_threads = atoi(optarg);
                if (opt_threads <= MAX_THREADS && opt_threads > 0) {
                    threads = opt_threads;
                } else {
                    printf("Error: -t is %d, but should be between 1 and %d, defaulting to 4\n", opt_threads, MAX_THREADS);
                }
                break;
                case 'm':
                for (char *p = optarg; *p; p++) {
                    if (!isdigit(*p)) {
                        printf("Error: -m requires a positive integer\n");
                        return EXIT_FAILURE;
                    }
                }
                int opt_min_threads = atoi(optarg);
                if (opt_min_threads <= MAX_THREADS && opt_min_threads > 0) {
                    min_threads = opt_min_threads;
                } else {
                    printf("Error: -m is %d, but should be between 1 and %d, defaulting to 1\n", opt_threads, MAX_THREADS);
                }
                break;
            case 'd':
                for (char *p = optarg; *p; p++) {
                    if (!isdigit(*p)) {
                        printf("Error: -d requires a positive integer\n");
                        return EXIT_FAILURE;
                    }
                }
                duration = atoi(optarg);
                break;
            case 's':
                smokewagon = true;
                break;
        }
    }
    if (min_threads > threads) {
        printf("min_threads (-m %ld) must be larger than threads (-t %ld)\n", min_threads, threads);
        return EXIT_FAILURE;
    }

    printf("filebacked mmap() microbenchmark, testing from %ld to %ld threads for %ld seconds each\n\n", min_threads, threads, duration);

    if (smokewagon) {
        mmap_flags |= MAP_PRIVATE_TLB;
        printf("smokewagon ON\n\n");
    } else {
        printf("smokewagon OFF\n\n");
    }

    // get and print uname
    struct utsname u;
    if (uname(&u) == -1) {
        perror("uname\n");
        return EXIT_FAILURE;
    }
    printf("running on: %s %s %s %s %s\n",
        u.sysname,
        u.nodename,
        u.release,
        u.version,
        u.machine);


    // each thread gets its own 1 GB virtual region to avoid page table lock contention
    // get this by mapping threads+1 GB and then picking aligned pointers from it
    char* big_mmap_ptr = mmap(NULL, ONE_GB_SIZE*(threads+1), PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (big_mmap_ptr == MAP_FAILED || big_mmap_ptr == NULL) {
        printf("big mmap failed\n");
        return -1;
    }
    // GB-aligned          big_mmap_ptr                              aligned_ptr
    // |-----unallocated---|----------offset_to_allocated------------|-------big-allocation-goes-on...
    size_t offset_to_unallocated = (uintptr_t) big_mmap_ptr % ONE_GB_SIZE;
    size_t offset_to_allocated = (ONE_GB_SIZE - offset_to_unallocated) % ONE_GB_SIZE;
    char* aligned_ptr = big_mmap_ptr + offset_to_allocated;

    // per-thread preparation
    for (int i=0; i<threads; i++) {
        thread_infos[i].tid = i; // assign each thread an id

        // carve off our chunk of the big allocation
        thread_infos[i].my_page = aligned_ptr;
        aligned_ptr += ONE_GB_SIZE;

        // punch a hole in our allocation that we'll map the file into later
        munmap(thread_infos[i].my_page, PAGE_SIZE);

        // each thread gets a bystander page to prevent freed_pages full-mm shootdown
        thread_infos[i].bystander_page = thread_infos[i].my_page + PAGE_SIZE;
        mprotect(thread_infos[i].bystander_page, PAGE_SIZE, PROT_READ|PROT_WRITE);
        thread_infos[i].bystander_page[0] = 'x';

        // could unmap the rest of our allocation, but why bother? faster vma traversal maybe?

        // open a file for each thread
        char filename[50];
        snprintf(filename, sizeof(filename), "microbenchmark-filebacked-temp-%02d", i);
        thread_infos[i].fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0600);
        if (thread_infos[i].fd == -1) {
            printf("file opening error!\n");
            return -1;
        }

        // extend file, 4 KiB per thread
        if (ftruncate(thread_infos[i].fd, PAGE_SIZE)) {
            printf("file truncation error!\n");
            close(thread_infos[i].fd);
            return -1;
        }

        // mmap file and write for warmup
        char* ptr = mmap(thread_infos[i].my_page, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED_NOREPLACE, thread_infos[i].fd, 0);
        if (ptr == NULL) {
            printf("mmap() for tid: %d failed, ptr == NULL\n", i);
            return -1;
        } else if (ptr == MAP_FAILED) {
            printf("mmap() for tid: %d failed, ptr == MAP_FAILED\n", i);
            return -1;
        } else if (ptr != thread_infos[i].my_page) {
            printf("mmap() for tid: %d problem, ptr != thread_infos[i].my_page\n", i);
            return -1;
        }
        ptr[0] = 'y';
        munmap(ptr, PAGE_SIZE);

        // set cpu affinities: https://man7.org/linux/man-pages/man3/pthread_setaffinity_np.3.html
        CPU_ZERO(&thread_infos[i].cpuset);
        CPU_SET(i, &thread_infos[i].cpuset);
        if (i == 0) {
        // set main thread's cpu affinity, which is already running
            pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &thread_infos[i].cpuset);
        } else {
            // set child threads' cpu affinities
            pthread_attr_init(&thread_infos[i].attr);
            pthread_attr_setaffinity_np(&thread_infos[i].attr, sizeof(cpu_set_t), &thread_infos[i].cpuset); // reusing pthread_attr without reinitializing below is fine
        }
        printf("tid %2d affinity set\n", i);
    }

    printf("\nbegin benchmarking\n\n");

    // main microbenchmarking loops
    for (long t=min_threads-1; t<threads; t++) {
        // set when benchmark ends
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        end = now.tv_sec * 1000000000L + now.tv_nsec + duration * 1000000000L;
        assert(end > now.tv_sec * 1000000000L + now.tv_nsec); // check overflow

        printf("Running %s map-read-unmap loop with %ld threads for %ld seconds:\n", smokewagon ? "smokewagon" : "baseline", t+1, duration);
        printf("now.tv_sec: %ld, now.tv_nsec: %ld, end: %ld\n", now.tv_sec, now.tv_nsec, end);

        // create and run threads
        for (long i=1; i<=t; i++) {
            thread_infos[i].return_value = pthread_create(&thread_infos[i].thread, &thread_infos[i].attr, test_smokewagon, &thread_infos[i]);
            if (thread_infos[i].return_value) printf("ERROR: return code for thread %ld from pthread_create() is %d\n", i, thread_infos[i].return_value);
        }

        test_smokewagon(&thread_infos[0]);

        // join created threads
        for (long i=1; i<=t ;i++) {
            pthread_join(thread_infos[i].thread, NULL);
            //if (threads[i].return_value) printf("ERROR: return code %d from thread %ld\n", threads[i].return_value, i);
        }

        // sum counters from each thread
        for (long i=0; i<=t; i++) {
            results[t] += thread_infos[i].counter;
            printf("tid %ld performed %lu loops\n", i, thread_infos[i].counter);
        }
        printf("%ld threads performed %ld %s loops in %ld seconds.\n\n", t+1, results[t], smokewagon ? "smokewagon" : "baseline", duration);
    }

    printf("microbenchmarking complete\n");

    munmap(big_mmap_ptr, ONE_GB_SIZE*(threads+1));
    // cleanup loop
    for (int i=0; i<threads; i++) {
        close(thread_infos[i].fd);
        char filename[50];
        snprintf(filename, sizeof(filename), "microbenchmark-filebacked-temp-%02d", i);
        remove(filename);
    }
    printf("testing files deleted\n\n");

    // output statistics
    const char* filename_prefix = "result-microbenchmark-filebacked-";
    const char* filename_smoke = "-smokewagon";
    const char* filename_base = "-baseline";
    const char* filename_suffix = ".csv";
    char* filename = malloc(strlen(filename_prefix) + strlen(u.release) + strlen (smokewagon ? filename_smoke : filename_base) + strlen(filename_suffix) + 1);
    if (!filename) {
        perror("output filename allocation failed");
    }
    strcpy(filename, filename_prefix);
    strcat(filename, u.release);
    strcat(filename, smokewagon ? filename_smoke : filename_base);
    strcat(filename, filename_suffix);

    printf("opening %s\n", filename);
    FILE *fptr = fopen(filename, "w");
    if(fptr == NULL) {
        perror("file opening error!");
        return EXIT_FAILURE;
    }

    fprintf(fptr, "threads,loops\n");
    for (long t=0; t<threads; t++) {
        fprintf(fptr, "%ld, %ld\n", t+1, results[t]);
    }
    fclose(fptr);
    printf("totals written to %s\n", filename);

    free(filename);

    /* don't destroy pthread_attr for t=0, since we didn't initialize it */
    for (long t=1; t<threads; t++) {
        pthread_attr_destroy(&thread_infos[t].attr);
    }
    printf("pthread attributes destroyed\n");

    return EXIT_SUCCESS;
}
