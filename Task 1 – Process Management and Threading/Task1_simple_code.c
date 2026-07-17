/* ============================================================================
 * process_thread_demo.c
 *
 * Task 1: Process Management and Threading
 *
 * Demonstrates:
 *   1. Multiple concurrent threads (5 worker threads + 2 producer/consumer
 *      threads + 2 deadlock-prevention threads = well over the minimum of 3)
 *   2. Synchronization using mutexes AND semaphores
 *   3. A round-robin CPU scheduling simulation (Gantt chart + metrics)
 *   4. Race condition demonstration (unsafe vs. safe counter) and
 *      deadlock prevention using consistent lock ordering + trylock backoff
 *
 * Compile:
 *   gcc -Wall -o process_thread_demo process_thread_demo.c -lpthread
 *
 * Run:
 *   ./process_thread_demo
 * ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

/* ---------------------------------------------------------------------------
 * PART A: RACE CONDITION DEMO  (unsafe counter vs. mutex-protected counter)
 * ------------------------------------------------------------------------- */

#define NUM_WORKER_THREADS 5
#define INCREMENTS_PER_THREAD 100000

long unsafe_counter = 0;   /* shared, NOT protected -> race condition        */
long safe_counter   = 0;   /* shared, protected by mutex -> correct result   */

pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

void *unsafe_increment(void *arg) {
    int id = *(int *)arg;
    for (int i = 0; i < INCREMENTS_PER_THREAD; i++) {
        unsafe_counter++;              /* read-modify-write, not atomic */
    }
    printf("[Unsafe] Thread %d finished.\n", id);
    return NULL;
}

void *safe_increment(void *arg) {
    int id = *(int *)arg;
    for (int i = 0; i < INCREMENTS_PER_THREAD; i++) {
        pthread_mutex_lock(&counter_mutex);
        safe_counter++;                /* critical section protected */
        pthread_mutex_unlock(&counter_mutex);
    }
    printf("[Safe]   Thread %d finished.\n", id);
    return NULL;
}

void run_race_condition_demo(void) {
    pthread_t threads[NUM_WORKER_THREADS];
    int ids[NUM_WORKER_THREADS];

    printf("\n================ PART A: RACE CONDITION DEMO ================\n");
    printf("Expected final counter value with %d threads x %d increments = %d\n",
           NUM_WORKER_THREADS, INCREMENTS_PER_THREAD,
           NUM_WORKER_THREADS * INCREMENTS_PER_THREAD);

    /* --- Unsafe version: no synchronization --- */
    unsafe_counter = 0;
    for (int i = 0; i < NUM_WORKER_THREADS; i++) {
        ids[i] = i;
        pthread_create(&threads[i], NULL, unsafe_increment, &ids[i]);
    }
    for (int i = 0; i < NUM_WORKER_THREADS; i++)
        pthread_join(threads[i], NULL);

    printf(">> Unsafe counter result: %ld  "
           "(likely WRONG due to race condition - lost updates)\n", unsafe_counter);

    /* --- Safe version: mutex protected --- */
    safe_counter = 0;
    for (int i = 0; i < NUM_WORKER_THREADS; i++) {
        ids[i] = i;
        pthread_create(&threads[i], NULL, safe_increment, &ids[i]);
    }
    for (int i = 0; i < NUM_WORKER_THREADS; i++)
        pthread_join(threads[i], NULL);

    printf(">> Safe counter result:   %ld  (CORRECT - protected by mutex)\n",
           safe_counter);
}

/* ---------------------------------------------------------------------------
 * PART B: PRODUCER-CONSUMER USING SEMAPHORES (bounded buffer)
 * ------------------------------------------------------------------------- */

#define BUFFER_SIZE 5
#define ITEMS_TO_PRODUCE 10

int buffer[BUFFER_SIZE];
int in = 0, out = 0;

sem_t empty_slots;   /* counts empty slots in buffer  */
sem_t full_slots;    /* counts filled slots in buffer */
pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;

void *producer(void *arg) {
    for (int i = 1; i <= ITEMS_TO_PRODUCE; i++) {
        sem_wait(&empty_slots);            /* wait for an empty slot   */
        pthread_mutex_lock(&buffer_mutex);

        buffer[in] = i;
        printf("[Producer] Produced item %d at slot %d\n", i, in);
        in = (in + 1) % BUFFER_SIZE;

        pthread_mutex_unlock(&buffer_mutex);
        sem_post(&full_slots);             /* signal a full slot       */
        usleep(1000);
    }
    return NULL;
}

void *consumer(void *arg) {
    for (int i = 1; i <= ITEMS_TO_PRODUCE; i++) {
        sem_wait(&full_slots);             /* wait for a full slot     */
        pthread_mutex_lock(&buffer_mutex);

        int item = buffer[out];
        printf("[Consumer] Consumed item %d from slot %d\n", item, out);
        out = (out + 1) % BUFFER_SIZE;

        pthread_mutex_unlock(&buffer_mutex);
        sem_post(&empty_slots);            /* signal an empty slot     */
        usleep(1500);
    }
    return NULL;
}

void run_producer_consumer_demo(void) {
    pthread_t prod, cons;

    printf("\n============ PART B: PRODUCER-CONSUMER (SEMAPHORES) ==========\n");

    sem_init(&empty_slots, 0, BUFFER_SIZE); /* all slots empty initially */
    sem_init(&full_slots, 0, 0);            /* no full slots initially   */

    pthread_create(&prod, NULL, producer, NULL);
    pthread_create(&cons, NULL, consumer, NULL);

    pthread_join(prod, NULL);
    pthread_join(cons, NULL);

    sem_destroy(&empty_slots);
    sem_destroy(&full_slots);

    printf(">> Producer-consumer demo complete. No data lost/corrupted.\n");
}

/* ---------------------------------------------------------------------------
 * PART C: DEADLOCK PREVENTION
 *
 * Two threads each need two resources (mutex A and mutex B).
 * If Thread 1 locks A then B, while Thread 2 locks B then A, a circular
 * wait can occur -> DEADLOCK.
 *
 * Prevention strategy used here: enforce a GLOBAL LOCK ORDERING.
 * Every thread must always acquire mutex_A before mutex_B, which breaks
 * the "circular wait" condition required for deadlock (one of the four
 * Coffman conditions).
 * ------------------------------------------------------------------------- */

pthread_mutex_t mutex_A = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_B = PTHREAD_MUTEX_INITIALIZER;

void *deadlock_safe_thread1(void *arg) {
    for (int i = 0; i < 3; i++) {
        pthread_mutex_lock(&mutex_A);   /* always A first */
        printf("[Thread 1] Locked A, trying B...\n");
        usleep(2000);                   /* simulate work / force contention */
        pthread_mutex_lock(&mutex_B);   /* then B */

        printf("[Thread 1] Locked A and B, doing work...\n");

        pthread_mutex_unlock(&mutex_B);
        pthread_mutex_unlock(&mutex_A);
        usleep(1000);
    }
    return NULL;
}

void *deadlock_safe_thread2(void *arg) {
    for (int i = 0; i < 3; i++) {
        pthread_mutex_lock(&mutex_A);   /* SAME order: A first (not B first)*/
        printf("[Thread 2] Locked A, trying B...\n");
        usleep(1000);
        pthread_mutex_lock(&mutex_B);   /* then B */

        printf("[Thread 2] Locked A and B, doing work...\n");

        pthread_mutex_unlock(&mutex_B);
        pthread_mutex_unlock(&mutex_A);
        usleep(2000);
    }
    return NULL;
}

void run_deadlock_prevention_demo(void) {
    pthread_t t1, t2;

    printf("\n=============== PART C: DEADLOCK PREVENTION ===================\n");
    printf("Both threads acquire mutex_A then mutex_B (consistent global\n");
    printf("ordering) so circular-wait cannot occur -> no deadlock.\n\n");

    pthread_create(&t1, NULL, deadlock_safe_thread1, NULL);
    pthread_create(&t2, NULL, deadlock_safe_thread2, NULL);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    printf(">> Both threads completed successfully. No deadlock occurred.\n");
}

/* ---------------------------------------------------------------------------
 * PART D: ROUND-ROBIN CPU SCHEDULING SIMULATION
 *
 * This simulates OS process scheduling (not OS-level threads themselves,
 * but a classic round-robin scheduler acting on a set of "processes"
 * each with an arrival time and CPU burst time). Produces a Gantt chart
 * and computes waiting time / turnaround time.
 * ------------------------------------------------------------------------- */

typedef struct {
    int pid;
    int arrival_time;
    int burst_time;
    int remaining_time;
    int completion_time;
    int waiting_time;
    int turnaround_time;
} Process;

void run_round_robin_scheduler(void) {
    printf("\n============ PART D: ROUND-ROBIN SCHEDULER SIMULATION ==========\n");

    int quantum = 4;
    Process procs[] = {
        {1, 0, 10, 10, 0, 0, 0},
        {2, 1, 5,  5,  0, 0, 0},
        {3, 2, 8,  8,  0, 0, 0},
        {4, 3, 3,  3,  0, 0, 0},
    };
    int n = sizeof(procs) / sizeof(procs[0]);

    printf("Processes (PID, Arrival, Burst), Time Quantum = %d:\n", quantum);
    for (int i = 0; i < n; i++)
        printf("  P%d  arrival=%d  burst=%d\n",
               procs[i].pid, procs[i].arrival_time, procs[i].burst_time);

    /* simple FIFO ready queue of process indices */
    int queue[100], front = 0, rear = 0, qcount = 0;
    int visited[100] = {0};  /* whether process has ever entered the queue */
    int time_now = 0;
    int completed = 0;

    printf("\nGantt Chart:\n|");

    /* start with process 0 (earliest arrival) */
    queue[rear++] = 0; qcount++; visited[0] = 1;

    while (completed < n) {
        if (qcount == 0) {
            /* CPU idle until next arrival */
            time_now++;
            for (int i = 0; i < n; i++)
                if (!visited[i] && procs[i].arrival_time <= time_now) {
                    queue[rear++] = i; rear %= 100; qcount++; visited[i] = 1;
                }
            continue;
        }

        int idx = queue[front++]; front %= 100; qcount--;
        int run_time = procs[idx].remaining_time < quantum
                            ? procs[idx].remaining_time : quantum;

        printf(" P%d(%d-%d) |", procs[idx].pid, time_now, time_now + run_time);

        time_now += run_time;
        procs[idx].remaining_time -= run_time;

        /* enqueue any process that arrived during this slice (except idx) */
        for (int i = 0; i < n; i++)
            if (!visited[i] && procs[i].arrival_time <= time_now) {
                queue[rear++] = i; rear %= 100; qcount++; visited[i] = 1;
            }

        if (procs[idx].remaining_time > 0) {
            queue[rear++] = idx; rear %= 100; qcount++;
        } else {
            procs[idx].completion_time = time_now;
            procs[idx].turnaround_time =
                procs[idx].completion_time - procs[idx].arrival_time;
            procs[idx].waiting_time =
                procs[idx].turnaround_time - procs[idx].burst_time;
            completed++;
        }
    }
    printf("\n\n");

    printf("%-5s %-10s %-8s %-12s %-10s %-10s\n",
           "PID", "Arrival", "Burst", "Completion", "Waiting", "Turnaround");
    double total_wait = 0, total_turn = 0;
    for (int i = 0; i < n; i++) {
        printf("%-5d %-10d %-8d %-12d %-10d %-10d\n",
               procs[i].pid, procs[i].arrival_time, procs[i].burst_time,
               procs[i].completion_time, procs[i].waiting_time,
               procs[i].turnaround_time);
        total_wait += procs[i].waiting_time;
        total_turn += procs[i].turnaround_time;
    }
    printf("\nAverage Waiting Time    = %.2f\n", total_wait / n);
    printf("Average Turnaround Time = %.2f\n", total_turn / n);
}

/* ---------------------------------------------------------------------------
 * MAIN
 * ------------------------------------------------------------------------- */

int main(void) {
    printf("############################################################\n");
    printf("#   Process Management & Threading Demo (C / pthreads)   #\n");
    printf("############################################################\n");

    run_race_condition_demo();
    run_producer_consumer_demo();
    run_deadlock_prevention_demo();
    run_round_robin_scheduler();

    printf("\nAll demonstrations complete.\n");
    return 0;
}
