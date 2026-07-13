/* ════════════════════════════════════════════════════════════════════════
   VIRTUALIA — An Interactive Virtual Memory & Page Replacement Simulator
   ────────────────────────────────────────────────────────────────────────
   ST5004CEM · Task 2 — Memory Management Simulation (25 marks)

   Satisfies:
     1. Paging system with page size configurable AT RUNTIME
     2. Two page replacement algorithms — FIFO and LRU (+ bonus: Optimal)
     3. Page-fault tracking and hit/miss ratio calculation
     4. Rich step-by-step visualization of memory allocation
   ════════════════════════════════════════════════════════════════════════ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_FRAMES   16
#define MAX_REF      64

/* ─────────────────────────── ANSI STYLE KIT ─────────────────────────── */
#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define DIM     "\033[2m"
#define ITAL    "\033[3m"
#define UNDER   "\033[4m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define WHITE   "\033[37m"
#define BG_CYAN "\033[46m"
#define BG_RED  "\033[41m"

/* ═══════════════════════════ DATA STRUCTURES ═══════════════════════════ */

typedef struct {
    int page_id;     /* -1 means the frame is empty                */
    int loaded_at;   /* tick this page entered the frame (FIFO)    */
    int last_used;   /* tick of most recent access (LRU)           */
} Frame;

typedef struct {
    int   hits;
    int   faults;
    int   evictions;
    int   peak_occupancy;
} Stats;

typedef struct {
    int  page_size;
    int  frame_count;
    int  ref_len;
    int  ref[MAX_REF];
} SimConfig;

/* ═══════════════════════════ SMALL UTILITIES ═══════════════════════════ */

static void hr(const char *ch, int n) {
    for (int i = 0; i < n; i++) printf("%s", ch);
}

static int get_int(const char *prompt, int lo, int hi) {
    int v; char buf[64];
    while (1) {
        printf(BOLD CYAN "  ➤ " RESET "%s" BOLD " [%d-%d]: " RESET, prompt, lo, hi);
        if (!fgets(buf, sizeof(buf), stdin)) exit(0);
        if (sscanf(buf, "%d", &v) == 1 && v >= lo && v <= hi) return v;
        printf(RED "    ⚠ Please enter a whole number between %d and %d.\n" RESET, lo, hi);
    }
}

static int get_choice(const char *prompt, int lo, int hi) {
    return get_int(prompt, lo, hi);
}

/* progress / ratio bar rendered with Unicode block glyphs */
static void draw_bar(float pct, const char *color) {
    int total = 30;
    int filled = (int)((pct / 100.0f) * total + 0.5f);
    if (filled > total) filled = total;
    printf("%s", color);
    for (int i = 0; i < filled; i++) printf("█");
    printf(RESET DIM);
    for (int i = filled; i < total; i++) printf("░");
    printf(RESET);
}

/* ═══════════════════════════ FRAME HELPERS ═══════════════════════════ */

static int find_page(Frame *f, int n, int page_id) {
    for (int i = 0; i < n; i++) if (f[i].page_id == page_id) return i;
    return -1;
}
static int first_empty(Frame *f, int n) {
    for (int i = 0; i < n; i++) if (f[i].page_id == -1) return i;
    return -1;
}
static int occupancy(Frame *f, int n) {
    int c = 0;
    for (int i = 0; i < n; i++) if (f[i].page_id != -1) c++;
    return c;
}

/* ═══════════════════════ VISUALIZATION PRIMITIVES ═══════════════════════ */

static void print_banner(void) {
    printf(BOLD MAGENTA
    "\n╔═══════════════════════════════════════════════════════════════════╗\n"
    "║   ██╗   ██╗██╗██████╗ ████████╗██╗   ██╗ █████╗ ██╗     ██╗ █████╗ ║\n"
    "║   ██║   ██║██║██╔══██╗╚══██╔══╝██║   ██║██╔══██╗██║     ██║██╔══██╗║\n"
    "║   ██║   ██║██║██████╔╝   ██║   ██║   ██║███████║██║     ██║███████║║\n"
    "║   ╚██╗ ██╔╝██║██╔══██╗   ██║   ██║   ██║██╔══██║██║     ██║██╔══██║║\n"
    "║    ╚████╔╝ ██║██║  ██║   ██║   ╚██████╔╝██║  ██║███████╗██║██║  ██║║\n"
    "║     ╚═══╝  ╚═╝╚═╝  ╚═╝   ╚═╝    ╚═════╝ ╚═╝  ╚═╝╚══════╝╚═╝╚═╝  ╚═╝║\n"
    "╚═══════════════════════════════════════════════════════════════════╝\n"
    RESET);
    printf(DIM ITAL "        A Virtual Memory & Page Replacement Playground\n" RESET);
}

static void print_config_summary(SimConfig *c) {
    printf(BOLD "\n  ┌─ Simulation Configuration " RESET);
    hr("─", 40); printf("\n");
    printf("  │  Page size      : " YELLOW "%d bytes" RESET "\n", c->page_size);
    printf("  │  Physical frames: " YELLOW "%d" RESET "  (%d bytes of RAM)\n",
           c->frame_count, c->frame_count * c->page_size);
    printf("  │  Reference len  : " YELLOW "%d" RESET " page requests\n", c->ref_len);
    printf("  │  Reference str  : " CYAN);
    for (int i = 0; i < c->ref_len; i++) printf("P%d ", c->ref[i]);
    printf(RESET "\n  └");
    hr("─", 68); printf("\n");
}

/* Every frame cell (filled or empty) renders to EXACTLY 6 visible columns:
   "[Pdd] " for a filled frame (bracket, P, 2-digit id, bracket, space) or
   "[ --] " for an empty frame (bracket, space, 2 dashes, bracket, space).
   Keeping both variants the same width is what keeps the table's vertical
   bars lined up regardless of how many frames are configured. */
#define CELL_WIDTH 6

/* total width of the "Physical Frames" column, wide enough to fit the
   column label itself even when frame_count is small */
static int frame_col_width(int frame_count) {
    int w = frame_count * CELL_WIDTH;
    int label_w = (int)strlen("Physical Frames");
    return (w > label_w) ? w : label_w;
}

/* draws the frame table header once per algorithm run */
static void print_table_header(const char *algo, const char *icon, int frame_count) {
    int fw = frame_col_width(frame_count);
    printf(BOLD BLUE "\n  ╭─ %s %s " RESET, icon, algo);
    hr("─", 8 + fw); printf("\n");
    printf(BOLD "  │ %-4s │ %-4s │ %-*s │ %-10s\n" RESET,
           "Tick", "Req", fw, "Physical Frames", "Outcome");
    printf(DIM "  ├──────┼──────┼");
    hr("─", fw + 2);
    printf("┼────────────\n" RESET);
}

static void print_table_footer(int frame_count) {
    int fw = frame_col_width(frame_count);
    printf(DIM "  └──────┴──────┴");
    hr("─", fw + 2);
    printf("┴────────────\n" RESET);
}

/* one row of the live frame visualization */
static void print_row(int tick, int ref, Frame *f, int n, int fault, int evicted_id) {
    int fw = frame_col_width(n);
    int printed = 0;

    printf("  │ %-4d │ P%-3d │ ", tick, ref);
    for (int i = 0; i < n; i++) {
        if (f[i].page_id == -1)
            printf(DIM "[ --]" RESET " ");
        else if (f[i].page_id == ref && fault)
            printf(BG_RED WHITE BOLD "[P%-2d]" RESET " ", f[i].page_id);
        else if (f[i].page_id == ref)
            printf(BG_CYAN WHITE BOLD "[P%-2d]" RESET " ", f[i].page_id);
        else
            printf(GREEN "[P%-2d]" RESET " ", f[i].page_id);
        printed += CELL_WIDTH;
    }
    /* pad out to the column width so the closing bar always lines up,
       even when frame_col_width() widened the column to fit the label */
    for (; printed < fw; printed++) printf(" ");

    if (fault) {
        if (evicted_id != -1)
            printf(" │ " RED BOLD "FAULT" RESET DIM " (evicted P%d)" RESET, evicted_id);
        else
            printf(" │ " RED BOLD "FAULT" RESET DIM " (free slot)" RESET);
    } else {
        printf(" │ " GREEN BOLD "HIT ✓" RESET);
    }
    printf("\n");
}

/* ═══════════════════════════ CORE ALGORITHMS ═══════════════════════════ */
/* Each algorithm returns Stats and prints its own live visualization.
   All three share the same Frame representation so behaviour is easy
   to compare and audit. */

static Stats run_fifo(SimConfig *cfg) {
    Frame f[MAX_FRAMES];
    for (int i = 0; i < cfg->frame_count; i++) f[i] = (Frame){-1, 0, 0};
    Stats s = {0, 0, 0, 0};

    print_table_header("FIFO — First In, First Out", "🥇", cfg->frame_count);
    for (int t = 0; t < cfg->ref_len; t++) {
        int ref = cfg->ref[t];
        int idx = find_page(f, cfg->frame_count, ref);

        if (idx != -1) {
            f[idx].last_used = t + 1;
            s.hits++;
            print_row(t + 1, ref, f, cfg->frame_count, 0, -1);
        } else {
            s.faults++;
            int evicted_id = -1;
            int slot = first_empty(f, cfg->frame_count);
            if (slot == -1) {
                int oldest = 0;
                for (int j = 1; j < cfg->frame_count; j++)
                    if (f[j].loaded_at < f[oldest].loaded_at) oldest = j;
                slot = oldest;
                evicted_id = f[slot].page_id;
                s.evictions++;
            }
            f[slot].page_id = ref;
            f[slot].loaded_at = t + 1;
            f[slot].last_used = t + 1;
            print_row(t + 1, ref, f, cfg->frame_count, 1, evicted_id);
        }
        int occ = occupancy(f, cfg->frame_count);
        if (occ > s.peak_occupancy) s.peak_occupancy = occ;
    }
    print_table_footer(cfg->frame_count);
    return s;
}

static Stats run_lru(SimConfig *cfg) {
    Frame f[MAX_FRAMES];
    for (int i = 0; i < cfg->frame_count; i++) f[i] = (Frame){-1, 0, 0};
    Stats s = {0, 0, 0, 0};

    print_table_header("LRU — Least Recently Used", "🕒", cfg->frame_count);
    for (int t = 0; t < cfg->ref_len; t++) {
        int ref = cfg->ref[t];
        int idx = find_page(f, cfg->frame_count, ref);

        if (idx != -1) {
            f[idx].last_used = t + 1;
            s.hits++;
            print_row(t + 1, ref, f, cfg->frame_count, 0, -1);
        } else {
            s.faults++;
            int evicted_id = -1;
            int slot = first_empty(f, cfg->frame_count);
            if (slot == -1) {
                int lru = 0;
                for (int j = 1; j < cfg->frame_count; j++)
                    if (f[j].last_used < f[lru].last_used) lru = j;
                slot = lru;
                evicted_id = f[slot].page_id;
                s.evictions++;
            }
            f[slot].page_id = ref;
            f[slot].loaded_at = t + 1;
            f[slot].last_used = t + 1;
            print_row(t + 1, ref, f, cfg->frame_count, 1, evicted_id);
        }
        int occ = occupancy(f, cfg->frame_count);
        if (occ > s.peak_occupancy) s.peak_occupancy = occ;
    }
    print_table_footer(cfg->frame_count);
    return s;
}

/* Bonus: Optimal (Belady's) replacement — evicts the page used farthest
   in the future (or never again). Included as an extra reference point;
   the assignment only requires FIFO + LRU, both fully implemented above. */
static Stats run_optimal(SimConfig *cfg) {
    Frame f[MAX_FRAMES];
    for (int i = 0; i < cfg->frame_count; i++) f[i] = (Frame){-1, 0, 0};
    Stats s = {0, 0, 0, 0};

    print_table_header("OPTIMAL — Belady's Min-Fault Oracle", "🔮", cfg->frame_count);
    for (int t = 0; t < cfg->ref_len; t++) {
        int ref = cfg->ref[t];
        int idx = find_page(f, cfg->frame_count, ref);

        if (idx != -1) {
            f[idx].last_used = t + 1;
            s.hits++;
            print_row(t + 1, ref, f, cfg->frame_count, 0, -1);
        } else {
            s.faults++;
            int evicted_id = -1;
            int slot = first_empty(f, cfg->frame_count);
            if (slot == -1) {
                int victim = 0, farthest = -1;
                for (int j = 0; j < cfg->frame_count; j++) {
                    int next_use = cfg->ref_len; /* "never again" */
                    for (int k = t + 1; k < cfg->ref_len; k++) {
                        if (cfg->ref[k] == f[j].page_id) { next_use = k; break; }
                    }
                    if (next_use > farthest) { farthest = next_use; victim = j; }
                }
                slot = victim;
                evicted_id = f[slot].page_id;
                s.evictions++;
            }
            f[slot].page_id = ref;
            f[slot].loaded_at = t + 1;
            f[slot].last_used = t + 1;
            print_row(t + 1, ref, f, cfg->frame_count, 1, evicted_id);
        }
        int occ = occupancy(f, cfg->frame_count);
        if (occ > s.peak_occupancy) s.peak_occupancy = occ;
    }
    print_table_footer(cfg->frame_count);
    return s;
}

/* ═══════════════════════════ RESULTS & REPORTING ═══════════════════════════ */

static void print_stats_card(const char *name, Stats s, int ref_len) {
    float hit_pct   = 100.0f * s.hits   / ref_len;
    float fault_pct = 100.0f * s.faults / ref_len;

    printf(BOLD "\n  ── %s : Hit / Miss Ratio ──\n" RESET, name);
    printf("   Hits   %5.1f%%  ", hit_pct);   draw_bar(hit_pct,   GREEN);  printf("  (%d)\n", s.hits);
    printf("   Faults %5.1f%%  ", fault_pct); draw_bar(fault_pct, RED);    printf("  (%d)\n", s.faults);
    printf(DIM "   Evictions: %d   Peak frame occupancy: %d\n" RESET, s.evictions, s.peak_occupancy);
}

static void print_comparison(SimConfig *cfg, Stats fifo, Stats lru, Stats opt) {
    printf(BOLD MAGENTA
        "\n╔═══════════════════════════════════════════════════════════════════╗\n"
        "║                    ALGORITHM SHOWDOWN                                ║\n"
        "╚═══════════════════════════════════════════════════════════════════╝\n"
        RESET);

    printf(BOLD "\n  %-12s %8s %8s %10s %12s\n" RESET,
           "Algorithm", "Hits", "Faults", "Hit Rate", "Evictions");
    printf(DIM "  "); hr("─", 54); printf("\n" RESET);

    Stats  arr[3]  = { fifo, lru, opt };
    const char *nm[3] = { "FIFO", "LRU", "Optimal*" };
    for (int i = 0; i < 3; i++) {
        float hr_pct = 100.0f * arr[i].hits / cfg->ref_len;
        printf("  %-12s " GREEN "%8d" RESET " " RED "%8d" RESET " %9.1f%% %12d\n",
               nm[i], arr[i].hits, arr[i].faults, hr_pct, arr[i].evictions);
    }

    printf("\n  " BOLD "Verdict: " RESET);
    if (lru.faults < fifo.faults)
        printf(GREEN "LRU beat FIFO — %d vs %d faults on this workload.\n" RESET, lru.faults, fifo.faults);
    else if (fifo.faults < lru.faults)
        printf(YELLOW "FIFO beat LRU — %d vs %d faults on this workload.\n" RESET, fifo.faults, lru.faults);
    else
        printf(CYAN "FIFO and LRU tied at %d faults each on this workload.\n" RESET, fifo.faults);

    printf(DIM "  * Optimal is a theoretical lower bound (needs future knowledge) —\n"
               "    shown only as a benchmark; it is not a real schedulable algorithm.\n"
               "  Note: FIFO can suffer Belady's Anomaly, where adding MORE frames\n"
               "  paradoxically increases faults. LRU never exhibits this problem.\n" RESET);
}

/* ═══════════════════════════ CONFIGURATION MENU ═══════════════════════════ */

static void build_reference_string(SimConfig *cfg) {
    printf(BOLD "\n  How should the reference string (page request sequence) be built?\n" RESET);
    printf("   1) Type it in manually\n");
    printf("   2) Generate randomly\n");
    printf("   3) Use a built-in demo pattern (shows locality + thrashing)\n");
    int choice = get_choice("Choose an option", 1, 3);

    if (choice == 1) {
        cfg->ref_len = get_int("How many page requests?", 1, MAX_REF);
        int max_page = get_int("Highest page number to allow (virtual pages are 0..N)", 1, 63);
        printf(DIM "  Enter each page number one at a time.\n" RESET);
        for (int i = 0; i < cfg->ref_len; i++) {
            char label[32];
            snprintf(label, sizeof(label), "Request #%d page id", i + 1);
            cfg->ref[i] = get_int(label, 0, max_page);
        }
    } else if (choice == 2) {
        cfg->ref_len = get_int("How many page requests?", 1, MAX_REF);
        int max_page = get_int("Highest page number to allow", 1, 63);
        unsigned seed = (unsigned)time(NULL);
        srand(seed);
        for (int i = 0; i < cfg->ref_len; i++) cfg->ref[i] = rand() % (max_page + 1);
        printf(DIM "  (random seed: %u)\n" RESET, seed);
    } else {
        int demo[] = {0, 1, 2, 3, 0, 1, 4, 0, 1, 2, 3, 4};
        cfg->ref_len = (int)(sizeof(demo) / sizeof(demo[0]));
        memcpy(cfg->ref, demo, sizeof(demo));
        printf(DIM "  Loaded demo pattern: shows a working set of pages 0-3, then\n"
                   "  a shift toward page 4 that forces steady eviction pressure.\n" RESET);
    }
}

static void configure_simulation(SimConfig *cfg) {
    print_banner();
    printf(BOLD "\n  Let's set up your virtual memory system.\n" RESET);
    cfg->page_size   = get_int("Page size in bytes (power-of-2 recommended, e.g. 4, 8, 16)", 1, 4096);
    cfg->frame_count = get_int("Number of physical frames in RAM", 1, MAX_FRAMES);
    build_reference_string(cfg);
    print_config_summary(cfg);
}

/* ═══════════════════════════ MAIN ═══════════════════════════ */

int main(void) {
    SimConfig cfg;
    configure_simulation(&cfg);

    printf(BOLD "\n  Press Enter to run the simulation...\n" RESET);
    getchar();

    Stats fifo = run_fifo(&cfg);
    print_stats_card("FIFO", fifo, cfg.ref_len);

    printf("\n");
    Stats lru = run_lru(&cfg);
    print_stats_card("LRU", lru, cfg.ref_len);

    printf("\n");
    Stats opt = run_optimal(&cfg);
    print_stats_card("Optimal (benchmark)", opt, cfg.ref_len);

    print_comparison(&cfg, fifo, lru, opt);

    printf("\n" DIM "  ── end of simulation ──\n\n" RESET);
    return 0;
}
