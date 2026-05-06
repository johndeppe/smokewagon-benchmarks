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

#define HUGEPAGE_SIZE 2097152
#define PAGE_SIZE   4096
#define MAX_THREADS 64
#define FILENAME "microbenchmark-filebacked"
#define MAP_PRIVATE_TLB 0x200000

struct __attribute__ ((aligned (64))) per_thread_info {
    long tid;
    pthread_t thread;
    pthread_attr_t attr;
    cpu_set_t cpuset;
    unsigned long counter;
    int return_value;
    char* my_page;
};

long threads = 4; 
long duration = 5;
long end;

int fd;

int protread;
int protwrite;
int mmap_flags;

void* test_smokewagon(void* info_ptr) {
    struct per_thread_info* my_info = info_ptr;
    long tid = my_info->tid;
    unsigned long local_counter = 0;
    struct timespec now;

    do {
        // mmap the thread's page in the file
        char* ptr = mmap(my_info->my_page, PAGE_SIZE, PROT_READ, MAP_PRIVATE_TLB|MAP_SHARED, fd, tid*HUGEPAGE_SIZE);

        // read
        if (ptr[0] != 'x') {
            printf("uhoh, tid: %ld misplaced its page somehow\n", tid);
        }

        // munmap page
        munmap(ptr, PAGE_SIZE);

        local_counter++;

        // check time
        clock_gettime(CLOCK_MONOTONIC, &now);
    } while ( end > now.tv_sec * 1000000000L + now.tv_nsec );

    my_info->counter = local_counter;

    return(info_ptr);
}

int main(int argc, char *argv[]) {
    struct per_thread_info thread_infos[MAX_THREADS];
    long results[MAX_THREADS][2] = {0}; // second dimension is smokewagon.  0 is off and 1 is on.

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

    printf("filebacked mmap() microbenchmark, testing from 1 to %ld threads for %ld seconds each\n", threads, duration);

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

    // open a file
    fd = open(FILENAME, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd == -1) {
        printf("file opening error!\n");
        return -1;
    }

    // extend file, 2MB for thread for PTE-lock spacing purposes
    int result = ftruncate(fd, threads*HUGEPAGE_SIZE);
    if (result == -1) {
        printf("file truncation error!\n");
        close(fd);
        return -1;
    }

    // mmap and write file for warmup
    char* ptr = mmap(NULL, threads*HUGEPAGE_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED) {
            printf("initial mmap() failed and returned NULL\n");
            return -1;
    }

    // grab a 2MB offset from the file pointer for each thread (to hopefully avoid page table lock contention)
    char* loop_ptr = ptr;
    for (size_t i=0; i<threads; i++) {
        thread_infos[i].my_page = loop_ptr;
    }
    munmap(ptr, threads*HUGEPAGE_SIZE);
    
    // per-thread preparation
    for (long i=0; i<threads; i++) {
        thread_infos[i].tid = i; // assign each thread an id

        // mmap each thread's pointer to its offset in the file, one small page's worth
        char* thread_ptr = (char*)mmap(thread_infos[i].my_page, PAGE_SIZE, PROT_WRITE|PROT_READ, MAP_SHARED, fd, i*HUGEPAGE_SIZE);
        if (thread_ptr == NULL || thread_ptr == MAP_FAILED) {
            printf("mmap() failed for tid: %ld's page, aborting\n", i);
            return -1;
        } else if (thread_ptr != thread_infos[i].my_page) {
            printf("uhoh, kernel moved tid: %ld's pointer from 0x%p to 0x%p\n", i, thread_infos[i].my_page, thread_ptr);
            thread_infos[i].my_page = thread_ptr;
        }
        // warmup write the first page
        for (size_t j=0; j<4096; j++) {
            thread_ptr[j] = 'x';
        }
        // then free it
        munmap(thread_ptr, PAGE_SIZE);
        
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
        printf("cpu affinities set\n");
    }

    printf("\nbegin benchmarking\n\n");

    // main microbenchmarking loops
    for (long t=0; t<threads; t++) {
    for (int smokewagon=0; smokewagon<2; smokewagon++) {
        // smokewagon == 0 means don't use smokewagon, smokewagon == 1 means use smokewagon
        mmap_flags = smokewagon ? MAP_SHARED : MAP_SHARED|MAP_PRIVATE_TLB;

        // set when benchmark ends
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        end = (duration + now.tv_sec) * 1000000000L + now.tv_nsec;
        assert(end > now.tv_sec * 1000000000L + now.tv_nsec); // check overflow

        printf("Running %s map-read-unmap loop with %ld threads for %ld seconds:\n", smokewagon ? "smokewagon" : "baseline", t+1, duration);

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
            results[t][smokewagon] += thread_infos[i].counter;
            printf("tid %ld performed %lu loops\n", i, thread_infos[i].counter);
        }
        printf("%ld threads performed %ld %s loops in %ld seconds.\n\n", t+1, results[t][smokewagon], smokewagon ? "smokewagon" : "baseline", duration);
    }
    }

    printf("microbenchmarking complete\n");
    close(fd);
    remove(FILENAME);
    printf("testing file deleted\n");

    const char* filename_prefix = "result-microbenchmark-filebacked-";
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

    fprintf(fptr, "threads,vanilla,smokewagon\n");
    for (long t=0; t<threads; t++) {
        fprintf(fptr, "%ld, %ld, %ld\n", t+1, results[t][0], results[t][1]);
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
