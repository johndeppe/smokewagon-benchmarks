#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
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

#define HUGEPAGE_SIZE 2097152
#define PAGE_SIZE   4096
#define MAX_THREADS 64

struct __attribute__ ((aligned (64))) per_thread_info {
    long tid;
    pthread_t thread;
    pthread_attr_t attr;
    cpu_set_t cpuset;
    unsigned long counter;
    int return_value;
    unsigned long* my_page;
};

long threads = 4; 
long duration = 5;
long end;

int protread;
int protwrite;

void* test_smokewagon(void* info_ptr) {
    struct per_thread_info* my_info = info_ptr;
    long tid = my_info->tid;
    unsigned long local_counter = 0;
    struct timespec now;

    do {
        clock_gettime(CLOCK_MONOTONIC, &now);

        // mprotect: https://man7.org/linux/man-pages/man2/mprotect.2.html

        // write page
        my_info->return_value = mprotect(my_info->my_page, PAGE_SIZE, protwrite);
        my_info->my_page[0] = local_counter;

        // read page
        my_info->return_value = mprotect(my_info->my_page, PAGE_SIZE, protread);
        assert(my_info->my_page[0] == local_counter); // read page

        local_counter++;
    } while ( end > now.tv_sec * 1000000000L + now.tv_nsec );

    my_info->counter = local_counter;

    return(info_ptr);
}

int main(int argc, char *argv[]) {
    struct per_thread_info thread_infos[MAX_THREADS];
    long results[MAX_THREADS][2][2] = {0}; // second dimension is smokewagon.  0 is off and 1 is on.
                                           // third dimension is readwrite, 0 is shootdown and 1 is no_prot_change so no shootdown

    // check opts
    int opt;
    char* endptr;
    while ((opt = getopt(argc, argv, "t:d:")) != -1) {
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
            case  'd':
                for (char *p = optarg; *p; p++) {
                    if (!isdigit(*p)) {
                        printf("Error: -d requires a positive integer\n");
                        return EXIT_FAILURE;
                    }
                }
                duration = atoi(optarg);
                break;
        }
    }

    printf("\nmprotect() microbenchmark, testing from 1 to %ld threads for %ld seconds each\n", threads, duration);

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

    for (long i=0; i<threads; i++) {
        thread_infos[i].tid = i; // assign each thread an id

        // allocate: https://man7.org/linux/man-pages/man3/malloc.3.html
        // we allocate a way larger area than needed (2MB / thread) to avoid contention at the kernel's PTE locks
        thread_infos[i].my_page = (unsigned long*)mmap(NULL, HUGEPAGE_SIZE, PROT_NONE, MAP_ANON|MAP_SHARED, -1, 0); // assign each thread a page
        if (thread_infos[i].my_page == NULL) {
            printf("mmap() failed and returned NULL\n");
        return -1;
        } else if (thread_infos[i].my_page == MAP_FAILED) {
            printf("mmap() failed with MAP_FAILED: %s\n", strerror(errno));
            return -1;
        } else {
            printf("allocated %d bytes (%dMB) at %p\n", HUGEPAGE_SIZE, HUGEPAGE_SIZE/(4096*256), thread_infos[i].my_page);
        }
        thread_infos[i].return_value = mprotect(thread_infos[i].my_page, PAGE_SIZE, PROT_READ|PROT_WRITE); // make thread's page writable
        // TODO check mprotect return value
        printf("fault-in thread %ld's page\n", i);
        thread_infos[i].my_page[0] = 0; // fault-in thread's page
        // set cpu affinities: https://man7.org/linux/man-pages/man3/pthread_setaffinity_np.3.html
        CPU_ZERO(&thread_infos[i].cpuset);
        CPU_SET(i, &thread_infos[i].cpuset);
        if (i == 0) {
        // set main thread's cpu affinity, which is already running
            pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &thread_infos[i].cpuset);
        } else {
            // set child threads' cpu affinities
            pthread_attr_init(&thread_infos[i].attr);
            pthread_attr_setaffinity_np(&thread_infos[i].attr, sizeof(cpu_set_t), &thread_infos[i].cpuset); // reusing these below is fine
        }
        printf("cpu affinities set\n");
    }
    
    printf("\nbegin benchmarking\n\n");

    // main microbenchmarking loops
    for (long t=0; t<threads; t++) {
        for (int smokewagon=0; smokewagon<2; smokewagon++) {
        for (int readwrite=0; readwrite<2; readwrite++) {
            // smokewagon 0 means don't use it, 1 means clear it
            for (long i=0; i<=t; i++) {
                madvise(thread_infos[i].my_page, PAGE_SIZE, smokewagon ? 26 : 27); // 26 is MADV_PRIVATE_TLB, 27 is MADV_NORMAL_TLB
            }

            // readwrite 0 means we're alternating, readwrite 1 means both mprotects are read and write and so won't shootdown
            protread  = readwrite ? PROT_READ|PROT_WRITE : PROT_READ;
            protwrite = readwrite ? PROT_READ|PROT_WRITE : PROT_WRITE;

            // set when benchmark ends
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            end = (duration + now.tv_sec) * 1000000000L + now.tv_nsec;
            assert(end > now.tv_sec * 1000000000L + now.tv_nsec); // check overflow

            printf("Running %s-%s mprotect loop with %ld threads for %ld seconds:\n", smokewagon ? "smokewagon" : "baseline", readwrite ? "no_prot_change" : "shootdown", t+1, duration);

            // create and run threads
            for (long i=1; i<=t; i++) {
                // thread_infos[i].return_value = pthread_create(&thread_infos[i].thread, &thread_infos[i].attr, test_smokewagon, &thread_infos[i]);
                thread_infos[i].return_value = pthread_create(&thread_infos[i].thread, NULL, test_smokewagon, &thread_infos[i]);
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
                results[t][smokewagon][readwrite] += thread_infos[i].counter;
                printf("tid %ld performed %lu loops\n", i, thread_infos[i].counter);
            }
            printf("%ld threads performed %ld %s-%s loops in %ld seconds.\n\n", t+1, results[t][smokewagon][readwrite], smokewagon ? "smokewagon" : "baseline", readwrite ? "no_prot_change" : "shootdown", duration);
        }
        }
    }

    const char* filename_prefix = "result-microbenchmark-mprotect-";
    const char* filename_suffix = ".csv";
    char* filename = malloc(strlen(filename_prefix) + strlen(u.release) + strlen(filename_suffix) + 1);
    if (!filename) {
        perror("filename allocation failed");
    }
    strcpy(filename, filename_prefix);
    strcat(filename, u.release);
    strcat(filename, filename_suffix);

    printf("opening %s\n", filename);
    FILE *fptr = fopen(filename, "w");
    if(fptr == NULL) {
        perror("file opening error!");
        return EXIT_FAILURE;
    }

    fprintf(fptr, "threads, baseline-shootdown, baseline-none, smokewagon-shootdown, smokewagon-none\n");
    for (long t=0; t<threads; t++) {
        fprintf(fptr, "%ld, %ld, %ld, %ld, %ld\n", t+1, results[t][0][0], results[t][0][1], results[t][1][0], results[t][1][1]);
    }
    fclose(fptr);
    printf("totals written to %s\n", filename);

    free(filename);

    /* don't destroy pthread_attr for t=0, since we didn't initialize it */
    for (long t=1; t<threads; t++) {
        pthread_attr_destroy(&thread_infos[t].attr);
    }
    printf("pthread attributes destroyed\n");

    for (long i=0; i<threads; i++) {
        munmap(thread_infos[i].my_page, HUGEPAGE_SIZE);
    }
    
    printf("testing area munmapped\n");

    return EXIT_SUCCESS;
}
