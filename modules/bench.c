#define _GNU_SOURCE /* for pthread_setaffinity_np GNU extension */
#include<pthread.h>
#include<string.h>
#include<stdio.h>
#include<stdbool.h>
#include<time.h>
#include<fcntl.h>
#include<stdlib.h> /* for exit */
#include<sched.h> /* for cpu affinity */
#include<unistd.h> /* for read/write */

#define DEVICE_FILE "/dev/ringbuf"
#define MSG_SIZE 256
#define TOTAL_SIZE (1U << 26) /* 64 MiB */
#define PROD_CPU 0
#define CONS_CPU 1

#define min(a, b) ((a) < (b) ? (a) : (b))

typedef struct spsc_args {
    bool fail;
    int fd;
    int cpu_id;
    size_t msg;
    size_t total;
} spsc_args;

static inline unsigned char patt(unsigned int n) {
    return (9 * n + 67) % 256;
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
   size_t p_total = 0;

   char *buf = malloc(a->msg);
   if (buf == NULL) {
       fprintf(stderr, "Failed to allocate buf in producer\n");
       exit(1);
   }
   while (p_total < a->total) {
       size_t chunk = min(a->msg, a->total - p_total);
       for (size_t i = 0; i < chunk; i++) buf[i] = patt(p_total + i);
       write_buf(a->fd, buf, chunk);
       p_total += chunk;
   }
   free(buf);
   return NULL;
}

static void *consumer_fn(void *arg) {
    spsc_args *a = arg;

    set_cpu_affine(a->cpu_id);

    size_t r_total = 0;

    char *buf = malloc(a->msg);
    if (buf == NULL) {
        fprintf(stderr, "Failed to allocate buf in consumer\n");
        exit(1);
    }
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
    free(buf);
    return NULL;
}

int main() {
    int fd = open(DEVICE_FILE, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Failed to open device file\n");
        exit(1);
    }

    spsc_args p_args = {0, fd, PROD_CPU, MSG_SIZE, TOTAL_SIZE};
    spsc_args c_args = {0, fd, CONS_CPU, MSG_SIZE, TOTAL_SIZE};

    pthread_t pt;
    pthread_t ct;

    struct timespec start, end;

    /* Start the measurement */
    clock_gettime(CLOCK_MONOTONIC, &start);

    pthread_create(&pt, NULL, producer_fn, &p_args);
    pthread_create(&ct, NULL, consumer_fn, &c_args);

    pthread_join(ct, NULL);
    pthread_join(pt, NULL);

    /* End the measurement */
    clock_gettime(CLOCK_MONOTONIC, &end);

    long seconds = end.tv_sec - start.tv_sec;
    long nanoseconds = end.tv_nsec - start.tv_nsec;
    
    /* nanoseconds might be less than zero */
    if (nanoseconds < 0) {
        seconds--;
        nanoseconds += 1000000000L;
    }

    double total_time_ms = (seconds * 1000.0) + (nanoseconds / 1000000.0);
    printf("Execution time: %.6f ms\n", total_time_ms);

    return 0;
}
