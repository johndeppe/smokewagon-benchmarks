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

#define PAGE_SIZE   4096
#define PAGES       1
#define MAX_THREADS 64

struct per_thread_info {
    long tid;
    pthread_t thread;
    pthread_attr_t thread_attr;
    cpu_set_t cpuset;
    unsigned long counter;
    int return_value;
};

long threads = 4; 
long duration = 5;
long end;

void* test_smokewagon(void* info_ptr) {
    struct per_thread_info* my_info = info_ptr;
    long tid = my_info->tid;
    unsigned long local_counter = 0;
    char* my_mmap_ptr;
    struct timespec now;

    do {
        // time check
        clock_gettime(CLOCK_MONOTONIC, &now);
        //printf("tid %ld: local_counter %lu, time: %ld\n", (long) tid, local_counter, end - now.tv_sec * 1000000000L - now.tv_nsec);

        // allocate: https://man7.org/linux/man-pages/man3/malloc.3.html
        my_mmap_ptr = (char*)mmap(NULL, PAGES*PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1, 0);
        if (my_mmap_ptr == NULL || my_mmap_ptr == MAP_FAILED) {
            my_info->return_value = -1;
            if (my_mmap_ptr == NULL) {
                printf("tid %ld: counter: %lu mmap() failed and returned NULL\n", tid, local_counter);
                continue;
            } else {
                printf("tid %ld: counter: %lu mmap() failed with MAP_FAILED: %s\n", tid, local_counter, strerror(errno));
                continue;
            }
        }

        // madvise: https://man7.org/linux/man-pages/man2/madvise.2.html
        my_info->return_value = madvise(my_mmap_ptr, PAGES*PAGE_SIZE, 26); // 26 advises MADV_PRIVATE_TLB
        if (my_info->return_value != 0) {
            //printf("tid %ld: madvise returned: %d, errno: %d, strerror: %s\n", tid, my_info.return_value, errno, strerror(errno));
        }

        // touch
        my_mmap_ptr[0] = 'x';

        // free and provoke TLB shootdown
        munmap(my_mmap_ptr, PAGES*PAGE_SIZE);

        local_counter++;
    } while ( end > now.tv_sec * 1000000000L + now.tv_nsec );

    my_info->counter = local_counter;
    return(info_ptr);
}

int main(int argc, char *argv[]) {
    struct per_thread_info thread_infos[MAX_THREADS];

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
                if (opt_threads < MAX_THREADS && opt_threads > 0) {
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

    // set main thread's cpu affinity: https://man7.org/linux/man-pages/man3/pthread_setaffinity_np.3.html
    CPU_ZERO(&thread_infos[0].cpuset);
    CPU_SET(0, &thread_infos[0].cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &thread_infos[0].cpuset);

    for (long t=1;t<=threads;t++) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        end = (duration + now.tv_sec) * 1000000000L + now.tv_nsec;
        assert(end > now.tv_sec * 1000000000L + now.tv_nsec); // token overflow check

        printf("Running m(un)map loop test with %ld threads for %ld seconds.\n", t, duration);

        // create threads
        for (long i=1;i<t;i++) {
            thread_infos[i].tid = i;
            thread_infos[i].return_value = pthread_create(&thread_infos[i].thread, NULL, test_smokewagon, &thread_infos[i]);
            if (thread_infos[i].return_value) printf("ERROR: return code for thread %ld from pthread_create() is %d\n", i, thread_infos[i].return_value);
        }

        test_smokewagon(&thread_infos[0]);

        // join created threads
        for (long i=1;i<t;i++) {
            pthread_join(thread_infos[i].thread, NULL);
            //if (threads[i].return_value) printf("ERROR: return code %d from thread %ld\n", threads[i].return_value, i);
        }

        long total = 0;
        for (long i=0;i<t;i++) {
            total += thread_infos[i].counter;
            printf("tid %ld performed %lu loops\n", i, thread_infos[i].counter);
        }
        printf("all %ld threads performed %ld loops in total\n\n", t, total);
    }
    return EXIT_SUCCESS;
}
