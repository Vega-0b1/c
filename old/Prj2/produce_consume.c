
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define BUF_SZ 5     // small buffer to make the blocking behavior obvious
#define NITEMS 1000  // how many items the producer will make and the consumer will read

static char buf[BUF_SZ];
static int in = 0;   // next position where producer writes
static int out = 0;  // next position where consumer reads

// synchronization primitives
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER; 
static sem_t empty_sem;  // counts empty slots (producer waits on this)
static sem_t full_sem;   // counts full  slots (consumer waits on this)

static void *producer(void *arg);
static void *consumer(void *arg);

int main(void) {
    pthread_t prod, cons;

    // Initialize semaphores:
    // empty_sem = BUF_SZ because all slots are empty at the start
    // full_sem  = 0      because there are no items yet
    sem_init(&empty_sem, 0, BUF_SZ);
    sem_init(&full_sem, 0, 0);

    // Create one producer and one consumer
    pthread_create(&prod, NULL, producer, NULL);
    pthread_create(&cons, NULL, consumer, NULL);

    // Wait for both threads to finish
    pthread_join(prod, NULL);
    pthread_join(cons, NULL);

    // Clean up semaphores
    sem_destroy(&empty_sem);
    sem_destroy(&full_sem);
    return 0;
}

static void *producer(void *arg) {

    for (int i = 0; i < NITEMS; i++) {
        char item = 'A' + (i % 26); // just cycle through A..Z

        // Wait until there is at least one empty slot
        sem_wait(&empty_sem);

        // Only one thread can touch buf/in/out at a time
        pthread_mutex_lock(&mtx);

        // Put the item in the buffer and move 'in' forward 
        buf[in] = item;
        in = (in + 1) % BUF_SZ; // circular buffer positioning

        pthread_mutex_unlock(&mtx);

        // Tell the consumer there's now one more full slot
        sem_post(&full_sem);

        // Small sleep so the output isn't a blur 
        usleep(1000);
    }
    return NULL;
}

static void *consumer(void *arg) {

    for (int i = 0; i < NITEMS; i++) {
        // Wait until there is at least one full slot
        sem_wait(&full_sem);

        // Only one thread can touch buf/in/out at a time
        pthread_mutex_lock(&mtx);

        // Take the item from the buffer and move 'out' forward (wrap around)
        char item = buf[out];
        out = (out + 1) % BUF_SZ;

        pthread_mutex_unlock(&mtx);

        // Tell the producer there's now one more empty slot
        sem_post(&empty_sem);

        // "Use" the item (here we just print it)
        putchar(item);
        fflush(stdout);

        // Small sleep so we can watch the letters stream out
        usleep(1000);
    }
    putchar('\n'); // final newline after all items
    return NULL;
}
