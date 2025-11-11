#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct { int P, B, S, F; } cfg_t;  // P: passengers, B: baggage handlers, S: screeners, F: flight attendants

// Counting semaphores (sizes are the number of workers at each stage)
static sem_t bag_sem, screen_sem, seat_sem;

// For the "all seated" notification
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;     // protects 'done'
static pthread_cond_t  done_cv  = PTHREAD_COND_INITIALIZER; // conditional variable
static int done = 0;                                        // number of passengers who finished

// Thread entry point
static void *passenger(void *arg);

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "usage: %s P B S F\n", argv[0]);
        return 1;
    }

    cfg_t c = { atoi(argv[1]), atoi(argv[2]), atoi(argv[3]), atoi(argv[4]) };
    if (c.P <= 0 || c.B <= 0 || c.S <= 0 || c.F <= 0) {
        fprintf(stderr, "all args must be > 0\n");
        return 1;
    }

    // Initialize semaphores with capacities equal to the number of workers
    sem_init(&bag_sem,    0, c.B);
    sem_init(&screen_sem, 0, c.S);
    sem_init(&seat_sem,   0, c.F);

    // Create one thread per passenger
    pthread_t *pt = calloc(c.P, sizeof(*pt));
    for (long i = 1; i <= c.P; i++)
        pthread_create(&pt[i-1], NULL, passenger, (void*)i);

    // Wait until all passengers are seated 
    pthread_mutex_lock(&mtx);
    while (done < c.P)
        pthread_cond_wait(&done_cv, &mtx);
    pthread_mutex_unlock(&mtx);

    printf("All %d passengers seated. Plane taking off!\n", c.P);

    // Join threads and clean up
    for (int i = 0; i < c.P; i++) pthread_join(pt[i], NULL);
    free(pt);

    sem_destroy(&bag_sem);
    sem_destroy(&screen_sem);
    sem_destroy(&seat_sem);
    return 0;
}

static void *passenger(void *arg) {
    long id = (long)arg;
    printf("Passenger #%ld arrived at the terminal.\n", id);

    // Stage 1: Baggage handling 
    printf("Passenger #%ld is waiting at baggage processing for a handler.\n", id);
    sem_wait(&bag_sem);       // wait for a handler
    usleep(2000);             // pretend work is happening
    sem_post(&bag_sem);       // release the handler

    // Stage 2: Security screening 
    printf("Passenger #%ld is waiting to be screened by a screener.\n", id);
    sem_wait(&screen_sem);    // wait for a screener
    usleep(2000);
    sem_post(&screen_sem);    // release the screener

    // Stage 3: Boarding/seating 
    printf("Passenger #%ld is waiting to board the plane by an attendant.\n", id);
    sem_wait(&seat_sem);      // wait for an attendant/seat
    usleep(2000);
    sem_post(&seat_sem);      // release the attendant/seat

    printf("Passenger #%ld has been seated and relaxes.\n", id);

    // Let main thread know we finished
    pthread_mutex_lock(&mtx);
    done++;
    pthread_cond_signal(&done_cv); // main waits until done == P
    pthread_mutex_unlock(&mtx);

    return NULL;
}
