#define _GNU_SOURCE /* for pthread_setaffinity_np GNU extension */
#include<pthread.h>
#include<string.h>
#include<stdio.h>
#include<stdbool.h>
#include<time.h>
#include<fcntl.h>
#include<stdlib.h> /* for exit, qsort */
#include<sched.h> /* for cpu affinity */
#include<unistd.h> /* for read/write */

#define DEVICE_FILE "/dev/ringbuf"
#define MSG_SIZE 256
#define MASK (MSG_SIZE-1)
#define TOTAL_SIZE (1U << 26) /* 64 MiB */
#define PROD_CPU 0
#define CONS_CPU 1

#define WARMUP 2  /* discarded runs to warm caches / branch predictors / freq */
#define ITERS 10  /* measured runs */

#define min(a, b) ((a) < (b) ? (a) : (b))

typedef struct spsc_args {
    bool fail;
    int fd;
    int cpu_id;
    size_t msg;
    size_t total;
} spsc_args;

/*
 * Three-way barriers (producer + consumer + main timer). The timer brackets
 * only the steady-state transfer: every party rendezvous at start_barrier
 * before the clock starts, and the timer waits on end_barrier so it stops only
 * once both worker threads have finished the iteration. Thread creation,
 * affinity setup and the malloc first-touch all happen before the first
 * barrier, so they are excluded from the measurement.
 */
static pthread_barrier_t start_barrier;
static pthread_barrier_t end_barrier;

static inline unsigned char patt(unsigned int n) {
    return (n + (n << 3) + 67) & MASK; 
}


static void set_cpu_affine(int cpu_id) {
    cpu_set_t cpu_set;

    /* Initialize the cpu_set structure */
    CPU_ZERO(&cpu_set);

    /* Set specific cpu for affinity */
    CPU_SET(cpu_id, &cpu_set);

    /* Set pthread to that cpu */
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpu_set) != 0) {
        fprintf(stderr, "Failed to set CPU affinity for core %d\n", cpu_id);
        exit(1);
    }
}

static void write_buf(int fd, char *buf, size_t size) {
    size_t total = 0;
    while (total < size) {
        ssize_t w = write(fd, buf + total, size - total);
        if (w < 0) {
            fprintf(stderr, "Failed to write in producer\n");
            exit(1);
        }
        total += (size_t)w;
    }
}

static void read_buf(int fd, char *buf, size_t size) {
    size_t total = 0;
    while (total < size) {
        ssize_t r = read(fd, buf + total, size - total);
        if (r < 0)   {
            fprintf(stderr, "Failed to read in consumer\n");
            exit(1);
        }
        total += (size_t)r;
    }
}

static void *producer_fn(void *arg) {

   spsc_args *a = arg;

   set_cpu_affine(a->cpu_id);

   char *buf = malloc(a->msg);
   if (buf == NULL) {
       fprintf(stderr, "Failed to allocate buf in producer\n");
       exit(1);
   }
   /* Touch the buffer so the page faults are not charged to the timer. */
   memset(buf, 0, a->msg);

   for (int it = 0; it < WARMUP + ITERS; it++) {
       pthread_barrier_wait(&start_barrier);

       size_t p_total = 0;
       while (p_total < a->total) {
           size_t chunk = min(a->msg, a->total - p_total);
           for (size_t i = 0; i < chunk; i++) buf[i] = patt(p_total + i);
           write_buf(a->fd, buf, chunk);
           p_total += chunk;
       }

       pthread_barrier_wait(&end_barrier);
   }
   free(buf);
   return NULL;
}

static void *consumer_fn(void *arg) {
    spsc_args *a = arg;

    set_cpu_affine(a->cpu_id);

    char *buf = malloc(a->msg);
    if (buf == NULL) {
        fprintf(stderr, "Failed to allocate buf in consumer\n");
        exit(1);
    }
    memset(buf, 0, a->msg);

    for (int it = 0; it < WARMUP + ITERS; it++) {
        pthread_barrier_wait(&start_barrier);

        size_t r_total = 0;
        while (r_total < a->total) {
           size_t chunk = min(a->msg, a->total - r_total);
           read_buf(a->fd, buf, chunk);
           for (size_t i = 0; i < chunk; i++) {
               if ((unsigned char)buf[i] != patt(r_total + i)) {
                   a->fail = true;
               }
           }
           r_total += chunk;
        }

        pthread_barrier_wait(&end_barrier);
    }
    free(buf);
    return NULL;
}

static int cmp_double(const void *x, const void *y) {
    double a = *(const double *)x;
    double b = *(const double *)y;
    return (a > b) - (a < b);
}

int main() {
    int fd = open(DEVICE_FILE, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Failed to open device file\n");
        exit(1);
    }

    spsc_args p_args = {0, fd, PROD_CPU, MSG_SIZE, TOTAL_SIZE};
    spsc_args c_args = {0, fd, CONS_CPU, MSG_SIZE, TOTAL_SIZE};

    pthread_barrier_init(&start_barrier, NULL, 3);
    pthread_barrier_init(&end_barrier, NULL, 3);

    pthread_t pt;
    pthread_t ct;

    pthread_create(&pt, NULL, producer_fn, &p_args);
    pthread_create(&ct, NULL, consumer_fn, &c_args);

    double samples[ITERS];

    for (int it = 0; it < WARMUP + ITERS; it++) {
        struct timespec start, end;

        /* Rendezvous, then start the clock for this iteration. */
        pthread_barrier_wait(&start_barrier);
        clock_gettime(CLOCK_MONOTONIC, &start);

        /* Block until both workers finish, then stop the clock. */
        pthread_barrier_wait(&end_barrier);
        clock_gettime(CLOCK_MONOTONIC, &end);

        long seconds = end.tv_sec - start.tv_sec;
        long nanoseconds = end.tv_nsec - start.tv_nsec;
        if (nanoseconds < 0) {
            seconds--;
            nanoseconds += 1000000000L;
        }
        double ms = (seconds * 1000.0) + (nanoseconds / 1000000.0);

        if (it < WARMUP) {
            printf("[warmup %d/%d] %.3f ms\n", it + 1, WARMUP, ms);
        } else {
            samples[it - WARMUP] = ms;
            printf("[run %2d/%d] %.3f ms\n", it - WARMUP + 1, ITERS, ms);
        }
    }

    pthread_join(ct, NULL);
    pthread_join(pt, NULL);

    pthread_barrier_destroy(&start_barrier);
    pthread_barrier_destroy(&end_barrier);

    if (p_args.fail || c_args.fail) {
        fprintf(stderr, "DATA MISMATCH: transferred bytes did not match pattern\n");
        return 1;
    }

    /* Summary statistics over the measured runs. */
    qsort(samples, ITERS, sizeof(samples[0]), cmp_double);

    double sum = 0;
    for (int i = 0; i < ITERS; i++) sum += samples[i];
    double mean = sum / ITERS;
    double med = (ITERS & 1)
        ? samples[ITERS / 2]
        : (samples[ITERS / 2 - 1] + samples[ITERS / 2]) / 2.0;
    double best = samples[0];
    double worst = samples[ITERS - 1];

    double mbytes = TOTAL_SIZE / 1000000.0;
    printf("\n--- %d runs (%d warmup discarded) ---\n", ITERS, WARMUP);
    printf("min   : %.3f ms  (%.1f MB/s)\n", best, mbytes / (best / 1000.0));
    printf("median: %.3f ms  (%.1f MB/s)\n", med, mbytes / (med / 1000.0));
    printf("mean  : %.3f ms\n", mean);
    printf("max   : %.3f ms\n", worst);
    printf("spread: %.1f%% (max/min - 1)\n", (worst / best - 1.0) * 100.0);

    return 0;
}
