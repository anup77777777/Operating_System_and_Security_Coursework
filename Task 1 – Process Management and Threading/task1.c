#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <time.h>

/* ═════════════════════════════════════════════════════════════════════
   ST5004CEM — Task 1: Process Management and Threading
   ─────────────────────────────────────────────────────────────────────
   This program satisfies every part of the assignment brief:

     1. PROCESS CREATION      -> fork() splits into a parent process
                                  (supervisor) and a child process that
                                  runs the whole thread simulation.
     2. THREADS (>= 3)        -> the child process spawns NUM_THREADS
                                  pthreads that run concurrently.
     3. SYNCHRONIZATION       -> pthread_mutex_t (mutexes) + a
                                  pthread_cond_t (condition variable,
                                  i.e. a monitor) coordinate the threads.
     4. ROUND-ROBIN SCHEDULER -> current_turn cycles 0 -> 1 -> 2 -> 0,
                                  each thread blocking until its turn.
     5. RACE CONDITION        -> shared_counter is only ever modified
        HANDLING                 inside counter_mutex, so concurrent
                                  increments can never corrupt it.
     6. DEADLOCK PREVENTION   -> resource_A is always locked before
                                  resource_B by every thread (fixed
                                  global lock ordering), which removes
                                  the possibility of circular wait.
   ═════════════════════════════════════════════════════════════════════ */

/* ─────────────────────────────────────────────────
   CONSTANTS
───────────────────────────────────────────────── */
#define NUM_THREADS 3
#define TIME_SLICE  1    /* seconds each thread works per round */
#define TOTAL_WORK  3    /* rounds each thread must complete    */
#define BAR_WIDTH   28   /* character width of progress bars    */

/* ─────────────────────────────────────────────────
   ANSI ESCAPE CODES — colours and effects
───────────────────────────────────────────────── */
#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define DIM     "\033[2m"
#define ITAL    "\033[3m"
#define UNDER   "\033[4m"

#define RED     "\033[38;5;203m"
#define GREEN   "\033[38;5;114m"
#define YELLOW  "\033[38;5;221m"
#define BLUE    "\033[38;5;75m"
#define MAGENTA "\033[38;5;176m"
#define CYAN    "\033[38;5;80m"
#define ORANGE  "\033[38;5;214m"
#define GREY    "\033[38;5;244m"
#define WHITE   "\033[38;5;255m"

#define BG_PANEL "\033[48;5;235m"

/* CLEAR screen + move cursor to top-left, hide cursor while drawing */
#define CLEAR      "\033[2J\033[H"
#define HIDE_CUR   "\033[?25l"
#define SHOW_CUR   "\033[?25h"

/* One colour per thread so they're visually distinct */
const char *T_COLOR[3] = {CYAN, MAGENTA, ORANGE};
const char *T_NAME[3]  = {"Thread-0", "Thread-1", "Thread-2"};

/* ─────────────────────────────────────────────────
   SHARED DATA + MUTEX
───────────────────────────────────────────────── */
int shared_counter = 0;
pthread_mutex_t counter_mutex;

/* ─────────────────────────────────────────────────
   ROUND-ROBIN SCHEDULER STATE
───────────────────────────────────────────────── */
int current_turn = 0;
pthread_mutex_t scheduler_mutex;
pthread_cond_t  turn_cond;

/* ─────────────────────────────────────────────────
   DEADLOCK-PREVENTION RESOURCES

   Deadlock needs 4 conditions at once: mutual exclusion,
   hold-and-wait, no preemption, and circular wait.
   PREVENTION: every thread always locks resource_A before
   resource_B, never the reverse. A fixed global order makes
   a circular chain of waits impossible, which breaks the
   circular-wait condition and rules out deadlock entirely.
───────────────────────────────────────────────── */
pthread_mutex_t resource_A;
pthread_mutex_t resource_B;

/* ─────────────────────────────────────────────────
   THREAD ARGUMENT + STATE STRUCT
───────────────────────────────────────────────── */
typedef struct {
    int thread_id;
    int rounds_done;
    char state[16]; /* "WAITING"|"RUNNING"|"LOCKING"|"RES-A"|"RES-B"|"DONE" */
} ThreadArgs;

ThreadArgs targs[NUM_THREADS];

/* ─────────────────────────────────────────────────
   MUTEX DISPLAY STATE (purely cosmetic, for the UI)
───────────────────────────────────────────────── */
int counter_mutex_locked = 0, counter_mutex_holder = -1;
int sched_mutex_locked   = 0, sched_mutex_holder   = -1;
int resA_locked = 0, resA_holder = -1;
int resB_locked = 0, resB_holder = -1;

pthread_mutex_t ui_mutex; /* prevents two threads drawing at once */
struct timespec sim_start;

/* ─────────────────────────────────────────────────
   SMALL DRAW HELPERS
───────────────────────────────────────────────── */

double elapsed_seconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - sim_start.tv_sec) +
           (now.tv_nsec - sim_start.tv_nsec) / 1e9;
}

/* Horizontal rule made of a repeated UTF-8 box character */
void hline(const char *color, const char *ch, int count) {
    printf("%s", color);
    for (int i = 0; i < count; i++) printf("%s", ch);
    printf(RESET);
}

/* ─────────────────────────────────────────────────
   BOXED-PANEL HELPERS

   box_top()/box_bottom() draw the border, box_line()
   draws one interior row. Padding is computed from
   strlen(content) instead of hand-counted spaces, so
   the right-hand border always lines up exactly under
   the top/bottom border, no matter what text goes in.
   IMPORTANT: content passed to box_line() must be
   plain ASCII (no emoji / em-dash / accented chars) —
   multi-byte UTF-8 characters make strlen() return a
   byte count that is larger than the visible column
   width, which is what throws the alignment off.
───────────────────────────────────────────────── */
#define BOX_WIDTH 64

void box_top(const char *color) {
    printf("%s  \xe2\x95\x94", color);
    for (int i = 0; i < BOX_WIDTH; i++) printf("\xe2\x95\x90");
    printf("\xe2\x95\x97" RESET "\n");
}

void box_bottom(const char *color) {
    printf("%s  \xe2\x95\x9a", color);
    for (int i = 0; i < BOX_WIDTH; i++) printf("\xe2\x95\x90");
    printf("\xe2\x95\x9d" RESET "\n");
}

void box_line(const char *border_color, const char *text_color, const char *content) {
    int len = (int)strlen(content);
    int pad = BOX_WIDTH - len;
    if (pad < 0) pad = 0;
    printf("%s  \xe2\x95\x91" RESET "%s%s%*s" RESET "%s\xe2\x95\x91" RESET "\n",
           border_color, text_color, content, pad, "", border_color);
}

/* Filled/empty gradient progress bar with a trailing percentage */
void print_bar(int done, int total, const char *color) {
    int filled = (done * BAR_WIDTH) / total;
    int pct = (done * 100) / total;
    printf("%s", color);
    for (int i = 0; i < BAR_WIDTH; i++)
        printf(i < filled ? "\xe2\x96\x88" : "\xe2\x96\x91"); /* █ or ░ */
    printf(RESET "  " BOLD "%3d%%" RESET, pct);
}

/* One row inside the mutex status panel */
void print_mutex_row(const char *label, int locked, int holder) {
    printf("  %s\xe2\x94\x82" RESET " %-15s ", GREY, label);
    if (locked)
        printf(RED "\xe2\x97\x8f LOCKED " RESET "by %s%-9s" RESET,
               T_COLOR[holder], T_NAME[holder]);
    else
        printf(GREEN "\xe2\x97\x8b FREE    " RESET "%-18s", "");
    printf("\n");
}

/* ─────────────────────────────────────────────────
   DRAW UI
   Clears the terminal and redraws the full dashboard.
   ui_mutex must be held by the caller.
───────────────────────────────────────────────── */
void draw_ui(void) {
    printf(CLEAR);

    /* ── Title banner ───────────────────────────── */
    box_top(BOLD BLUE);
    box_line(BOLD BLUE, WHITE, "   ST5004CEM - Task 1: Process Management & Threading");
    box_bottom(BOLD BLUE);
    printf("\n");

    printf(DIM "  Parent PID %d supervising child PID %d" RESET
           DIM "   \xe2\x8f\xb1  %.1fs elapsed\n\n" RESET,
           (int)getppid(), (int)getpid(), elapsed_seconds());

    /* ── Shared counter ─────────────────────────── */
    int target = NUM_THREADS * TOTAL_WORK;
    printf(BOLD "  Shared Counter" RESET "   " GREEN BOLD "%d" RESET
           DIM " / %d" RESET "   " DIM "(%d threads \xc3\x97 %d rounds each)\n\n" RESET,
           shared_counter, target, NUM_THREADS, TOTAL_WORK);

    /* ── Thread table ────────────────────────────── */
    printf(BOLD "  %-11s %-8s %-38s %s\n" RESET,
           "THREAD", "ROUNDS", "PROGRESS", "STATE");
    hline(GREY, "\xe2\x94\x80", 74); printf("\n");

    for (int i = 0; i < NUM_THREADS; i++) {
        ThreadArgs *t = &targs[i];

        const char *sc =
            strcmp(t->state, "DONE")    == 0 ? GREEN  :
            strcmp(t->state, "RUNNING") == 0 ? T_COLOR[i] :
            strcmp(t->state, "LOCKING") == 0 ? YELLOW :
            strcmp(t->state, "RES-A")   == 0 ? YELLOW :
            strcmp(t->state, "RES-B")   == 0 ? YELLOW : GREY;

        const char *dot = strcmp(t->state, "WAITING") == 0 ? "\xe2\x97\x8b" : "\xe2\x97\x8f";

        printf("  %s%s %-9s" RESET " %s%d / %d%s     ",
               T_COLOR[i], dot, T_NAME[i], BOLD, t->rounds_done, TOTAL_WORK, RESET);
        print_bar(t->rounds_done, TOTAL_WORK, T_COLOR[i]);
        printf("  %s%-9s" RESET "\n", sc, t->state);
    }

    /* ── Scheduler + mutex panel side note ──────── */
    printf("\n" BOLD "  Scheduler turn " RESET "\xe2\x86\x92 %s%s" RESET "\n\n",
           T_COLOR[current_turn], T_NAME[current_turn]);

    printf(BOLD "  Synchronization primitives\n" RESET);
    print_mutex_row("counter_mutex",   counter_mutex_locked, counter_mutex_holder);
    print_mutex_row("scheduler_mutex", sched_mutex_locked,   sched_mutex_holder);
    print_mutex_row("resource_A",      resA_locked,          resA_holder);
    print_mutex_row("resource_B",      resB_locked,          resB_holder);

    /* ── Legend ──────────────────────────────────── */
    printf("\n" DIM "  Legend: " RESET
           GREY "\xe2\x97\x8b WAITING  " RESET
           "%s\xe2\x97\x8f RUNNING  " RESET
           YELLOW "\xe2\x97\x8f LOCKING/RES-A/RES-B  " RESET
           GREEN "\xe2\x97\x8f DONE" RESET "\n",
           WHITE);

    /* ── Deadlock note ───────────────────────────── */
    printf("\n" DIM
           "  Deadlock prevention: every thread locks resource_A before\n"
           "  resource_B, with no exceptions. Fixed lock ordering removes\n"
           "  circular wait, so deadlock cannot occur under any schedule.\n" RESET);
}

/* ─────────────────────────────────────────────────
   THREAD FUNCTION
   Each loop iteration = one round of work:
     1. Wait for our turn        (round-robin)
     2. Simulate work            (sleep TIME_SLICE)
     3. Lock counter_mutex       -> increment -> unlock
     4. Lock resource_A then resource_B (ordered)
        -> use both -> unlock B then A (deadlock prevention)
     5. Pass turn to next thread
───────────────────────────────────────────────── */
void *thread_task(void *arg) {
    ThreadArgs *t = (ThreadArgs *)arg;
    int id = t->thread_id;

    for (int round = 0; round < TOTAL_WORK; round++) {

        /* ── Step 1: wait for our turn ─────────────── */
        pthread_mutex_lock(&ui_mutex);
        strcpy(t->state, "WAITING");
        draw_ui();
        pthread_mutex_unlock(&ui_mutex);

        pthread_mutex_lock(&scheduler_mutex);

        pthread_mutex_lock(&ui_mutex);
        sched_mutex_locked = 1;
        sched_mutex_holder = id;
        pthread_mutex_unlock(&ui_mutex);

        while (current_turn != id)
            pthread_cond_wait(&turn_cond, &scheduler_mutex);

        pthread_mutex_lock(&ui_mutex);
        sched_mutex_locked = 0;
        sched_mutex_holder = -1;
        pthread_mutex_unlock(&ui_mutex);

        pthread_mutex_unlock(&scheduler_mutex);

        /* ── Step 2: simulate work ──────────────────── */
        pthread_mutex_lock(&ui_mutex);
        strcpy(t->state, "RUNNING");
        draw_ui();
        pthread_mutex_unlock(&ui_mutex);

        sleep(TIME_SLICE);

        /* ── Step 3: update shared counter safely ───── */
        pthread_mutex_lock(&ui_mutex);
        strcpy(t->state, "LOCKING");
        counter_mutex_locked = 1;
        counter_mutex_holder = id;
        draw_ui();
        pthread_mutex_unlock(&ui_mutex);

        pthread_mutex_lock(&counter_mutex);      /* CRITICAL SECTION */
        shared_counter++;
        t->rounds_done++;
        pthread_mutex_unlock(&counter_mutex);    /* END CRITICAL SECTION */

        pthread_mutex_lock(&ui_mutex);
        counter_mutex_locked = 0;
        counter_mutex_holder = -1;
        draw_ui();
        pthread_mutex_unlock(&ui_mutex);

        /* ── Step 4: deadlock-safe two-resource access ─
         * Every thread locks resource_A FIRST, then resource_B,
         * always in this order, never reversed.
         */
        pthread_mutex_lock(&ui_mutex);
        strcpy(t->state, "RES-A");
        draw_ui();
        pthread_mutex_unlock(&ui_mutex);

        pthread_mutex_lock(&resource_A);
        pthread_mutex_lock(&ui_mutex);
        resA_locked = 1;
        resA_holder = id;
        strcpy(t->state, "RES-B");
        draw_ui();
        pthread_mutex_unlock(&ui_mutex);

        pthread_mutex_lock(&resource_B);
        pthread_mutex_lock(&ui_mutex);
        resB_locked = 1;
        resB_holder = id;
        draw_ui();
        pthread_mutex_unlock(&ui_mutex);

        /* ... pretend to do work needing both resources ... */

        pthread_mutex_unlock(&resource_B);
        pthread_mutex_lock(&ui_mutex);
        resB_locked = 0;
        resB_holder = -1;
        draw_ui();
        pthread_mutex_unlock(&ui_mutex);

        pthread_mutex_unlock(&resource_A);
        pthread_mutex_lock(&ui_mutex);
        resA_locked = 0;
        resA_holder = -1;
        draw_ui();
        pthread_mutex_unlock(&ui_mutex);

        /* ── Step 5: pass turn to next thread ──────── */
        pthread_mutex_lock(&scheduler_mutex);

        pthread_mutex_lock(&ui_mutex);
        sched_mutex_locked = 1;
        sched_mutex_holder = id;
        pthread_mutex_unlock(&ui_mutex);

        current_turn = (current_turn + 1) % NUM_THREADS;
        pthread_cond_broadcast(&turn_cond); /* wake all, each checks its turn */

        pthread_mutex_lock(&ui_mutex);
        sched_mutex_locked = 0;
        sched_mutex_holder = -1;
        pthread_mutex_unlock(&ui_mutex);

        pthread_mutex_unlock(&scheduler_mutex);
    }

    pthread_mutex_lock(&ui_mutex);
    strcpy(t->state, "DONE");
    draw_ui();
    pthread_mutex_unlock(&ui_mutex);

    return NULL;
}

/* ─────────────────────────────────────────────────
   run_simulation()
   Everything that used to live in main() — now run
   inside the CHILD process created by fork(). This
   is where all NUM_THREADS pthreads are created,
   run to completion, and joined.
───────────────────────────────────────────────── */
void run_simulation(void) {
    pthread_t threads[NUM_THREADS];

    pthread_mutex_init(&counter_mutex,   NULL);
    pthread_mutex_init(&scheduler_mutex, NULL);
    pthread_mutex_init(&ui_mutex,        NULL);
    pthread_mutex_init(&resource_A,      NULL);
    pthread_mutex_init(&resource_B,      NULL);
    pthread_cond_init(&turn_cond,        NULL);

    clock_gettime(CLOCK_MONOTONIC, &sim_start);

    for (int i = 0; i < NUM_THREADS; i++) {
        targs[i].thread_id   = i;
        targs[i].rounds_done = 0;
        strcpy(targs[i].state, "WAITING");
    }

    printf(HIDE_CUR);
    draw_ui();

    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, thread_task, &targs[i]) != 0) {
            fprintf(stderr, "Error: could not create thread %d\n", i);
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < NUM_THREADS; i++)
        pthread_join(threads[i], NULL);

    pthread_mutex_lock(&ui_mutex);
    draw_ui();

    int target = NUM_THREADS * TOTAL_WORK;
    char summary_line[BOX_WIDTH + 32];

    printf("\n");
    box_top(BOLD GREEN);
    box_line(BOLD GREEN, GREEN, "   [OK] All threads completed - no deadlock occurred.");
    snprintf(summary_line, sizeof(summary_line),
             "   Final shared counter = %d (expected %d)", shared_counter, target);
    box_line(BOLD GREEN, RESET, summary_line);
    box_bottom(BOLD GREEN);
    printf("\n");
    printf(SHOW_CUR);
    pthread_mutex_unlock(&ui_mutex);

    pthread_mutex_destroy(&counter_mutex);
    pthread_mutex_destroy(&scheduler_mutex);
    pthread_mutex_destroy(&ui_mutex);
    pthread_mutex_destroy(&resource_A);
    pthread_mutex_destroy(&resource_B);
    pthread_cond_destroy(&turn_cond);
}

/* ─────────────────────────────────────────────────
   MAIN
   Demonstrates PROCESS creation with fork():
     - The CHILD process runs the entire multithreaded
       round-robin simulation (run_simulation()).
     - The PARENT process simply supervises: it waits
       for the child to exit and reports its exit status,
       which is the classic fork()/wait() pattern for
       process management.
───────────────────────────────────────────────── */
int main(void) {
    printf(BOLD "\n  Forking supervisor process...\n" RESET);

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork failed");
        exit(EXIT_FAILURE);

    } else if (pid == 0) {
        /* ── CHILD PROCESS ──────────────────────────
           Runs the full thread-based simulation.     */
        run_simulation();
        exit(EXIT_SUCCESS);

    } else {
        /* ── PARENT PROCESS ─────────────────────────
           Supervises the child and reports how it
           exited. This is the process-management half
           of the assignment: fork() + wait().         */
        int status;
        waitpid(pid, &status, 0);

        printf(BOLD BLUE "\n  [Parent %d] child process %d finished. "
               RESET, (int)getpid(), (int)pid);

        if (WIFEXITED(status))
            printf(GREEN "Exit code: %d\n\n" RESET, WEXITSTATUS(status));
        else
            printf(RED "Child terminated abnormally.\n\n" RESET);
    }

    return 0;
}
