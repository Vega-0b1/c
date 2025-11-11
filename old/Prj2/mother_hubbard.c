#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define NCHILD 12
enum { TASKS = 4 }; // 0..3 = breakfast, school, dinner, bath (Mother). Father: read + tuck.

static int days;
static sem_t mother_sem; // Mother starts awake (1)
static sem_t father_sem; // Father starts asleep (0)
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

static int state[NCHILD];

static void *mother(void *arg);
static void *father(void *arg);

static void mother_task(int c, int t);
static void father_task(int c, int t);

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s {days}\n", argv[0]);
        return 1;
    }
    days = atoi(argv[1]);
    if (days <= 0) {
        fprintf(stderr, "Number of days must be > 0\n");
        return 1;
    }

    sem_init(&mother_sem, 0, 1); // Mother can start immediately each day
    sem_init(&father_sem, 0, 0); // Father blocks until at least one bath happens

    // Create the two threads
    pthread_t tm, tf;
    pthread_create(&tm, NULL, mother, NULL);
    pthread_create(&tf, NULL, father, NULL);

    // Wait for both to finish
    pthread_join(tm, NULL);
    pthread_join(tf, NULL);

    // Cleanup
    sem_destroy(&mother_sem);
    sem_destroy(&father_sem);
    return 0;
}

static void *mother(void *arg) {
    (void)arg;

    // Repeat the schedule for the requested number of days
    for (int day = 1; day <= days; day++) {
        // Mother "wakes up" blocks here until allowed to start the day
        sem_wait(&mother_sem);
        printf("This is day #%d of a day in the life of Mother Hubbard.\n", day);
        printf("Mother wakes up.\n");

        pthread_mutex_lock(&mtx);
        for (int i = 0; i < NCHILD; i++) state[i] = 0;
        pthread_mutex_unlock(&mtx);

        // Mother does all four tasks for each child, in order.
        for (int task = 0; task < 4; task++) {
            for (int c = 0; c < NCHILD; c++) {
                mother_task(c, task);

                // Once we hit "bath" (task == 3), Father is allowed to wake up.
                if (task == 3) sem_post(&father_sem);
            }
        }

        // Mother sleeps until Father finishes reading/tucking everyone and wakes her.
        printf("Mother is going to sleep.\n");
    }
    return NULL;
}

static void *father(void *arg) {
    (void)arg;

    for (int day = 1; day <= days; day++) {
        // Father blocks until at least one bath has happened
        sem_wait(&father_sem);
        printf("Father wakes up.\n");

        // For each child: read a book then tuck in 
        for (int c = 0; c < NCHILD; c++) {
            father_task(c, 0); // read
            father_task(c, 1); // tuck
        }

        // Father finishes and "wakes" Mother to start the next day or finish if last
        printf("Father is going to sleep and waking up mother to take care children.\n");
        sem_post(&mother_sem);
    }
    return NULL;
}

static void mother_task(int c, int t) {
    static const char *msg[] = {
        "fed breakfast",
        "taken to school",
        "given dinner",
        "given a bath"
    };
    printf("Child #%d is being %s.\n", c + 1, msg[t]);
    usleep(100); 
}

static void father_task(int c, int t) {
    static const char *msg[] = {
        "being read a book",
        "being tucked in bed"
    };
    printf("Child #%d is %s by father.\n", c + 1, msg[t]);
    usleep(100); 
}
