/* ============================================================================
   TASK 3 : SECURE FILE MANAGEMENT SYSTEM  (v2 - Advanced Edition)
   ----------------------------------------------------------------------------
   A single-file C implementation demonstrating core Operating-System concepts:

     1. File Operations       - create / open / read / write / append /
                                 delete / rename
     2. User Authentication   - login, salted+iterated password hashing,
                                 password verification, self-registration
     3. Permission System     - Unix-style rwx bits for Owner / Group / Other,
                                 simple preset-based permission editor
     4. Encryption             - passphrase-based XOR stream cipher with a
                                 checksum-based integrity/authenticity check
     5. Audit Logging         - every security-relevant event is timestamped
                                 and appended to audit.log

   Build :   gcc task3.c -o task3
   Run   :   ./task3

   UI note: every box / border / menu column in this program is drawn with a
   *computed* padding routine (see the "UI ENGINE" section) rather than
   hand-typed spaces, so alignment never breaks no matter how long a label,
   username, or filename is.

   Crypto note: this is an educational assignment, not a production system.
   The password hash is a salted, iterated custom hash (a simplified
   PBKDF-style construction) and the file cipher is an XOR stream cipher
   keyed from a user passphrase. Both exist purely to demonstrate the
   *concepts* of salted hashing and symmetric encryption inside a
   self-contained program that needs no external crypto library.
   ============================================================================
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>

/* --------------------------------------------------------------------------
   CONFIG / CONSTANTS
   -------------------------------------------------------------------------- */
#define MAX_USERS      64
#define MAX_FILES      256
#define MAX_UNAME      32
#define MAX_FNAME      128
#define MAX_LINE       4096
#define VAULT_DIR      "vault"
#define USERS_DB       "users.dat"
#define META_DB        "filemeta.dat"
#define AUDIT_LOG      "audit.log"
#define HASH_ROUNDS    5000
#define BOX_W          64   /* inner width used by every box/panel/menu */

/* --------------------------------------------------------------------------
   ANSI COLOR / STYLE HELPERS
   -------------------------------------------------------------------------- */
#define RESET     "\033[0m"
#define BOLD      "\033[1m"
#define DIM       "\033[2m"
#define UNDER     "\033[4m"
#define RED       "\033[31m"
#define GREEN     "\033[32m"
#define YELLOW    "\033[33m"
#define BLUE      "\033[34m"
#define MAGENTA   "\033[35m"
#define CYAN      "\033[36m"
#define WHITE     "\033[37m"
#define BRED      "\033[91m"
#define BGREEN    "\033[92m"
#define BYELLOW   "\033[93m"
#define BBLUE     "\033[94m"
#define BMAGENTA  "\033[95m"
#define BCYAN     "\033[96m"

/* --------------------------------------------------------------------------
   DATA STRUCTURES
   -------------------------------------------------------------------------- */
typedef struct {
    char username[MAX_UNAME];
    char group[MAX_UNAME];
    char salt[17];       /* 16 hex chars + NUL            */
    char passhash[65];   /* 64 hex chars + NUL            */
    int  is_admin;
    int  active;
} User;

typedef struct {
    char filename[MAX_FNAME];
    char owner[MAX_UNAME];
    char group[MAX_UNAME];
    int  perm[3][3];     /* [owner|group|other][r|w|x] -> 0/1 */
    int  encrypted;
    unsigned long checksum; /* integrity check for encrypted content */
    time_t created;
    time_t modified;
    int  active;
} FileMeta;

typedef struct {
    int num;
    const char *label;
} MenuItem;

static User users[MAX_USERS];
static int  user_count = 0;
static FileMeta files[MAX_FILES];
static int  file_count = 0;
static User *current_user = NULL;

/* ============================================================================
   UI ENGINE  -- every border/box/column below is computed, never hand-spaced
   ============================================================================ */
static void clear_screen(void) { printf("\033[2J\033[H"); fflush(stdout); }

/* draw a horizontal rule: left + (fill repeated BOX_W times) + right */
static void hline(const char *left, const char *fill, const char *right, const char *color) {
    printf("%s%s", color, left);
    for (int i = 0; i < BOX_W; i++) printf("%s", fill);
    printf("%s%s\n", right, RESET);
}

/* draw one row of a box: vert + centered text (padded to BOX_W) + vert */
static void box_row(const char *vert, const char *border_color,
                     const char *text, const char *text_style) {
    int len = (int)strlen(text);
    int pad = BOX_W - len;
    if (pad < 0) pad = 0;
    int left = pad / 2, right = pad - left;
    printf("%s%s%s", border_color, vert, RESET);
    printf("%s", text_style);
    for (int i = 0; i < left; i++) putchar(' ');
    printf("%s", text);
    for (int i = 0; i < right; i++) putchar(' ');
    printf("%s", RESET);
    printf("%s%s%s\n", border_color, vert, RESET);
}

/* a left-aligned row (used for status bar details) */
static void box_row_left(const char *vert, const char *border_color,
                          const char *text, const char *text_style) {
    int len = (int)strlen(text);
    int pad = BOX_W - 2 - len; /* 2 = left margin */
    if (pad < 0) pad = 0;
    printf("%s%s%s", border_color, vert, RESET);
    printf("  %s%s", text_style, text);
    for (int i = 0; i < pad; i++) putchar(' ');
    printf("%s", RESET);
    printf("%s%s%s\n", border_color, vert, RESET);
}

static void box_blank(const char *vert, const char *color) { box_row(vert, color, "", ""); }

static void banner(void) {
    hline("+", "=", "+", BCYAN);
    box_blank("|", BCYAN);
    box_row("|", BCYAN, "S E C U R E   F I L E   V A U L T", BOLD);
    box_row("|", BCYAN, "Operating Systems  |  Task 3 Project", RESET);
    box_row("|", BCYAN, "Advanced Edition v2.0", DIM);
    box_blank("|", BCYAN);
    hline("+", "=", "+", BCYAN);
}

static void status_bar(void) {
    const char *role_color = current_user->is_admin ? BYELLOW : BCYAN;
    const char *role_label = current_user->is_admin ? "ADMINISTRATOR" : "STANDARD USER";
    char line[160];
    snprintf(line, sizeof(line), "user: %s   group: %s   role: %s",
             current_user->username, current_user->group, role_label);
    hline("+", "-", "+", role_color);
    box_row_left("|", role_color, line, BOLD);
    hline("+", "-", "+", role_color);
}

/* a labelled sub-rule used to open every screen, e.g. section_rule("CREATE FILE") */
static void section_rule(const char *title, const char *color) {
    char buf[96];
    snprintf(buf, sizeof(buf), " %s ", title);
    int len = (int)strlen(buf);
    printf("\n%s%s%s", color, buf, RESET);
    printf("%s", color);
    for (int i = len; i < BOX_W; i++) printf("-");
    printf("%s\n\n", RESET);
}

/* full screen header used by every action: clears, shows banner + status,
   then a colored section rule with the screen's title */
static void action_header(const char *title) {
    clear_screen();
    banner();
    if (current_user) { printf("\n"); status_bar(); }
    section_rule(title, BBLUE);
}

static void pause_enter(void) {
    printf(DIM "Press ENTER to continue..." RESET);
    int c; while ((c = getchar()) != '\n' && c != EOF) {}
}

/* consistent, color-coded feedback messages */
static void say_ok(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    printf(BGREEN "  [OK] " RESET);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}
static void say_err(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    printf(BRED "  [X] " RESET);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}
static void say_warn(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    printf(BYELLOW "  [!] " RESET);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}
static void say_info(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    printf(BCYAN "  [i] " RESET);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

/* animated progress bar, e.g. used during authentication */
static void progress_bar(const char *label, int width) {
    printf(YELLOW "  %s  " RESET, label);
    fflush(stdout);
    printf("[");
    for (int i = 0; i < width; i++) {
        printf(BGREEN "#" RESET);
        fflush(stdout);
        usleep(30000);
    }
    printf("] " BGREEN "done" RESET "\n");
}

/* prints a left / right pair of menu items with computed (not guessed)
   column width, so numbering like "9)" vs "12)" never breaks alignment */
static void print_menu_two_col(MenuItem *items, int n) {
    char bufs[16][96];
    int maxw = 0;
    for (int i = 0; i < n; i++) {
        int w = snprintf(bufs[i], sizeof(bufs[i]), "%2d) %s", items[i].num, items[i].label);
        if (w > maxw) maxw = w;
    }
    for (int i = 0; i < n; i += 2) {
        if (i + 1 < n) {
            printf("    " BYELLOW "%-*s" RESET "   " BYELLOW "%s" RESET "\n",
                   maxw, bufs[i], bufs[i + 1]);
        } else {
            printf("    " BYELLOW "%s" RESET "\n", bufs[i]);
        }
    }
}

/* trim newline from fgets input */
static void strip_nl(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r')) s[--n] = '\0';
}

static void read_line(const char *prompt, char *buf, int max) {
    printf("  %s%s%s", CYAN, prompt, RESET);
    fflush(stdout);
    if (fgets(buf, max, stdin) == NULL) { buf[0] = '\0'; return; }
    strip_nl(buf);
}

/* masked password entry using raw terminal mode */
static void read_password(const char *prompt, char *buf, int max) {
    printf("  %s%s%s", CYAN, prompt, RESET);
    fflush(stdout);
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~((unsigned)(ICANON | ECHO));
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    int i = 0;
    int ch;
    while (i < max - 1 && (ch = getchar()) != '\n' && ch != EOF) {
        if (ch == 127 || ch == 8) {           /* backspace */
            if (i > 0) { i--; printf("\b \b"); fflush(stdout); }
        } else if (ch >= 32 && ch <= 126) {
            buf[i++] = (char)ch;
            printf("*");
            fflush(stdout);
        }
    }
    buf[i] = '\0';
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    printf("\n");
}

/* sanitize a filename to prevent path traversal / directory tricks */
static int valid_filename(const char *name) {
    if (strlen(name) == 0 || strlen(name) >= MAX_FNAME) return 0;
    if (strstr(name, "..") != NULL) return 0;
    if (strchr(name, '/') != NULL || strchr(name, '\\') != NULL) return 0;
    for (size_t i = 0; i < strlen(name); i++) {
        if (!(isalnum((unsigned char)name[i]) || name[i] == '.' ||
              name[i] == '_' || name[i] == '-')) return 0;
    }
    return 1;
}

/* ============================================================================
   PERSISTENCE : users / file metadata
   ============================================================================ */
static void ensure_vault_dir(void) {
    struct stat st;
    if (stat(VAULT_DIR, &st) == -1) mkdir(VAULT_DIR, 0700);
}

static void load_users(void) {
    FILE *f = fopen(USERS_DB, "rb");
    if (!f) { user_count = 0; return; }
    user_count = 0;
    User u;
    while (fread(&u, sizeof(User), 1, f) == 1 && user_count < MAX_USERS) {
        users[user_count++] = u;
    }
    fclose(f);
}

static void save_users(void) {
    FILE *f = fopen(USERS_DB, "wb");
    if (!f) { perror("save_users"); return; }
    fwrite(users, sizeof(User), (size_t)user_count, f);
    fclose(f);
}

static void load_files(void) {
    FILE *f = fopen(META_DB, "rb");
    if (!f) { file_count = 0; return; }
    file_count = 0;
    FileMeta m;
    while (fread(&m, sizeof(FileMeta), 1, f) == 1 && file_count < MAX_FILES) {
        files[file_count++] = m;
    }
    fclose(f);
}

static void save_files(void) {
    FILE *f = fopen(META_DB, "wb");
    if (!f) { perror("save_files"); return; }
    fwrite(files, sizeof(FileMeta), (size_t)file_count, f);
    fclose(f);
}

/* ============================================================================
   AUDIT LOGGING
   ============================================================================ */
static void audit_log(const char *actor, const char *action,
                       const char *target, const char *result) {
    FILE *f = fopen(AUDIT_LOG, "a");
    if (!f) return;
    time_t now = time(NULL);
    char tbuf[64];
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(f, "[%s] user=%-10s action=%-16s target=%-20s result=%s\n",
            tbuf, actor ? actor : "-", action, target ? target : "-", result);
    fclose(f);
}

/* ============================================================================
   HASHING (salted, iterated -- for password storage, NOT for real security)
   ============================================================================ */
static unsigned long djb2(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) hash = ((hash << 5) + hash) + (unsigned long)c;
    return hash;
}
static unsigned long sdbm(const char *str) {
    unsigned long hash = 0;
    int c;
    while ((c = *str++)) hash = (unsigned long)c + (hash << 6) + (hash << 16) - hash;
    return hash;
}

static void gen_salt(char *out) {
    static const char hexch[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) out[i] = hexch[rand() % 16];
    out[16] = '\0';
}

/* produces a 64-hex-char digest from password+salt, iterated HASH_ROUNDS
   times to slow down brute-force guessing (poor-man's key stretching). */
static void hash_password(const char *password, const char *salt, char *out_hex) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s$%s", salt, password);
    unsigned long h1 = djb2(buf);
    unsigned long h2 = sdbm(buf);
    for (int r = 0; r < HASH_ROUNDS; r++) {
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%lx%lx%d", h1, h2, r);
        h1 = djb2(tmp);
        h2 = sdbm(tmp);
    }
    unsigned long seedvals[8];
    unsigned long mix = h1 ^ (h2 * 2654435761UL);
    for (int i = 0; i < 8; i++) {
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%lu-%d-%lu", mix, i, h2);
        mix = djb2(tmp) ^ sdbm(tmp);
        seedvals[i] = mix;
    }
    char *p = out_hex;
    for (int i = 0; i < 8; i++) {
        p += sprintf(p, "%08lx", (seedvals[i] & 0xFFFFFFFFUL));
    }
    *p = '\0';
}

/* ============================================================================
   XOR STREAM CIPHER + CHECKSUM (file encryption)
   ============================================================================ */
static void xor_crypt(unsigned char *data, size_t len, const char *key) {
    size_t klen = strlen(key);
    if (klen == 0) return;
    unsigned char ks[256];
    for (int i = 0; i < 256; i++) {
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%s#%d", key, i);
        ks[i] = (unsigned char)(djb2(tmp) & 0xFF);
    }
    for (size_t i = 0; i < len; i++) {
        data[i] ^= ks[i % 256];
        data[i] ^= (unsigned char)key[i % klen];
    }
}

static unsigned long checksum_buf(const unsigned char *data, size_t len) {
    unsigned long h = 5381;
    for (size_t i = 0; i < len; i++) h = ((h << 5) + h) + data[i];
    return h;
}

/* ============================================================================
   USER LOOKUP / AUTH
   ============================================================================ */
static int find_user(const char *username) {
    for (int i = 0; i < user_count; i++)
        if (users[i].active && strcmp(users[i].username, username) == 0) return i;
    return -1;
}

static int find_file(const char *filename) {
    for (int i = 0; i < file_count; i++)
        if (files[i].active && strcmp(files[i].filename, filename) == 0) return i;
    return -1;
}

static void create_default_admin(void) {
    if (user_count > 0) return;
    User u;
    memset(&u, 0, sizeof(u));
    strncpy(u.username, "admin", MAX_UNAME - 1);
    strncpy(u.group, "admins", MAX_UNAME - 1);
    gen_salt(u.salt);
    hash_password("admin123", u.salt, u.passhash);
    u.is_admin = 1;
    u.active = 1;
    users[user_count++] = u;
    save_users();
    printf("\n");
    say_warn("No users found. A default administrator account was created:");
    printf("      username: " BOLD "admin" RESET "   password: " BOLD "admin123" RESET "\n");
    printf(BRED "      Please change this password after logging in!\n" RESET);
    audit_log("system", "INIT", "admin", "SUCCESS");
}

static int register_user(void) {
    action_header("CREATE NEW ACCOUNT");
    char uname[MAX_UNAME], group[MAX_UNAME], pass1[128], pass2[128];
    read_line("Choose a username: ", uname, MAX_UNAME);
    if (strlen(uname) == 0 || find_user(uname) != -1) {
        say_err("Invalid or already-taken username.");
        audit_log(uname, "REGISTER", uname, "FAIL-duplicate");
        pause_enter();
        return 0;
    }
    read_line("Group name (e.g. staff, guests): ", group, MAX_UNAME);
    if (strlen(group) == 0) strncpy(group, "users", MAX_UNAME - 1);

    read_password("Choose a password: ", pass1, sizeof(pass1));
    read_password("Confirm password : ", pass2, sizeof(pass2));
    if (strcmp(pass1, pass2) != 0) {
        say_err("Passwords do not match.");
        audit_log(uname, "REGISTER", uname, "FAIL-mismatch");
        pause_enter();
        return 0;
    }
    if (strlen(pass1) < 4) {
        say_err("Password too short (min 4 characters).");
        pause_enter();
        return 0;
    }
    if (user_count >= MAX_USERS) {
        say_err("User table full.");
        pause_enter();
        return 0;
    }
    User u;
    memset(&u, 0, sizeof(u));
    strncpy(u.username, uname, MAX_UNAME - 1);
    strncpy(u.group, group, MAX_UNAME - 1);
    gen_salt(u.salt);
    hash_password(pass1, u.salt, u.passhash);
    u.is_admin = 0;
    u.active = 1;
    users[user_count++] = u;
    save_users();
    printf("\n");
    say_ok("Account '%s' created. You may now log in.", uname);
    audit_log(uname, "REGISTER", uname, "SUCCESS");
    pause_enter();
    return 1;
}

static int login(void) {
    action_header("LOGIN");
    char uname[MAX_UNAME], pass[128];
    read_line("Username: ", uname, MAX_UNAME);
    read_password("Password: ", pass, sizeof(pass));
    printf("\n");
    progress_bar("Authenticating", 20);

    int idx = find_user(uname);
    if (idx == -1) {
        say_err("Authentication failed: unknown user.");
        audit_log(uname, "LOGIN", "-", "FAIL-nouser");
        return 0;
    }
    char computed[65];
    hash_password(pass, users[idx].salt, computed);
    if (strcmp(computed, users[idx].passhash) != 0) {
        say_err("Authentication failed: wrong password.");
        audit_log(uname, "LOGIN", "-", "FAIL-badpass");
        return 0;
    }
    current_user = &users[idx];
    say_ok("Welcome back, %s!", current_user->username);
    audit_log(uname, "LOGIN", "-", "SUCCESS");
    return 1;
}

static void change_password(void) {
    char old[128], n1[128], n2[128];
    read_password("Current password: ", old, sizeof(old));
    char check[65];
    hash_password(old, current_user->salt, check);
    if (strcmp(check, current_user->passhash) != 0) {
        say_err("Incorrect current password.");
        audit_log(current_user->username, "CHANGE_PW", "-", "FAIL");
        return;
    }
    read_password("New password: ", n1, sizeof(n1));
    read_password("Confirm new password: ", n2, sizeof(n2));
    if (strcmp(n1, n2) != 0 || strlen(n1) < 4) {
        say_err("Passwords did not match or too short.");
        audit_log(current_user->username, "CHANGE_PW", "-", "FAIL");
        return;
    }
    gen_salt(current_user->salt);
    hash_password(n1, current_user->salt, current_user->passhash);
    save_users();
    say_ok("Password updated.");
    audit_log(current_user->username, "CHANGE_PW", "-", "SUCCESS");
}

/* ============================================================================
   PERMISSIONS
   ============================================================================ */
static void mode_str(FileMeta *fm, char *out) {
    const char *labels = "rwx";
    int p = 0;
    for (int who = 0; who < 3; who++)
        for (int bit = 0; bit < 3; bit++)
            out[p++] = fm->perm[who][bit] ? labels[bit] : '-';
    out[p] = '\0';
}

/* action: 'r','w','x'  returns 1 if allowed, 0 otherwise */
static int check_permission(FileMeta *fm, User *u, char action) {
    if (u->is_admin) return 1; /* admin bypasses all checks */
    int bit = (action == 'r') ? 0 : (action == 'w') ? 1 : 2;
    int who;
    if (strcmp(fm->owner, u->username) == 0) who = 0;
    else if (strcmp(fm->group, u->group) == 0) who = 1;
    else who = 2;
    return fm->perm[who][bit];
}

static void parse_octal_perm(int octal_digit, int perm[3]) {
    perm[0] = (octal_digit & 4) ? 1 : 0; /* r */
    perm[1] = (octal_digit & 2) ? 1 : 0; /* w */
    perm[2] = (octal_digit & 1) ? 1 : 0; /* x */
}

/* ask a yes/no question, showing the current value as the default if you
   just hit ENTER (so you don't have to retype everything) */
static int read_yn(const char *prompt, int default_val) {
    char buf[8];
    char full_prompt[160];
    snprintf(full_prompt, sizeof(full_prompt), "%s [%s]: ",
             prompt, default_val ? "Y/n" : "y/N");
    read_line(full_prompt, buf, sizeof(buf));
    if (strlen(buf) == 0) return default_val;
    return (buf[0] == 'y' || buf[0] == 'Y');
}

/* interactive toggle: walks Owner/Group/Other x Read/Write/Execute,
   defaulting to whatever the file currently has, so you never need to
   remember or type an octal digit */
static void chmod_interactive(FileMeta *fm) {
    const char *who_label[3] = { "Owner", "Group", "Other" };
    for (int who = 0; who < 3; who++) {
        printf("\n  %s%s%s\n", BOLD, who_label[who], RESET);
        fm->perm[who][0] = read_yn("    Read", fm->perm[who][0]);
        fm->perm[who][1] = read_yn("    Write", fm->perm[who][1]);
        fm->perm[who][2] = read_yn("    Execute", fm->perm[who][2]);
    }
}

/* ============================================================================
   FILE OPERATIONS
   ============================================================================ */
static char *vault_path(const char *filename, char *out, size_t outsz) {
    snprintf(out, outsz, "%s/%s", VAULT_DIR, filename);
    return out;
}

static void list_files(void) {
    action_header("FILE LISTING");
    printf("  %s%-18s %-10s %-10s %-10s %-6s %-10s%s\n", BOLD,
           "NAME", "OWNER", "GROUP", "PERMS", "ENC", "SIZE", RESET);
    hline("  +", "-", "+", DIM);
    int shown = 0;
    for (int i = 0; i < file_count; i++) {
        if (!files[i].active) continue;
        char perms[10];
        mode_str(&files[i], perms);
        char path[300];
        vault_path(files[i].filename, path, sizeof(path));
        struct stat st;
        long size = (stat(path, &st) == 0) ? (long)st.st_size : -1;
        printf("  %-18s %-10s %-10s %s%-10s%s %-6s %-10ld\n",
               files[i].filename, files[i].owner, files[i].group,
               files[i].encrypted ? YELLOW : GREEN, perms, RESET,
               files[i].encrypted ? "YES" : "no", size);
        shown++;
    }
    if (shown == 0) printf(DIM "  (vault is empty)\n" RESET);
    printf("\n");
    pause_enter();
}

static void create_file_op(void) {
    action_header("CREATE FILE");
    char fname[MAX_FNAME];
    read_line("Filename: ", fname, MAX_FNAME);
    if (!valid_filename(fname)) {
        say_err("Invalid filename (no slashes, no '..', alnum/./_/- only).");
        audit_log(current_user->username, "CREATE", fname, "FAIL-badname");
        pause_enter(); return;
    }
    if (find_file(fname) != -1) {
        say_err("A file with that name already exists.");
        audit_log(current_user->username, "CREATE", fname, "FAIL-exists");
        pause_enter(); return;
    }
    if (file_count >= MAX_FILES) {
        say_err("Vault is full.");
        pause_enter(); return;
    }

    printf("\n");
    MenuItem presets[] = {
        {1, "Private (only I can read/write)"},
        {2, "Share with my group (they can read)"},
        {3, "Share with my group (they can read+write)"},
        {4, "Public read-only (everyone can read)"},
        {0, "Use default (private)"}
    };
    print_menu_two_col(presets, 5);
    printf("\n");
    char choice[8];
    read_line("Choose starting permissions: ", choice, sizeof(choice));
    int c = atoi(choice);
    int o = 6, g = 0, ot = 0;
    switch (c) {
        case 2: o = 6; g = 4; ot = 0; break;
        case 3: o = 6; g = 6; ot = 0; break;
        case 4: o = 6; g = 4; ot = 4; break;
        case 1:
        case 0:
        default: o = 6; g = 0; ot = 0; break;
    }

    char path[300];
    vault_path(fname, path, sizeof(path));
    FILE *f = fopen(path, "wb");
    if (!f) {
        say_err("Could not create file on disk.");
        audit_log(current_user->username, "CREATE", fname, "FAIL-io");
        pause_enter(); return;
    }
    fclose(f);

    FileMeta fm;
    memset(&fm, 0, sizeof(fm));
    strncpy(fm.filename, fname, MAX_FNAME - 1);
    strncpy(fm.owner, current_user->username, MAX_UNAME - 1);
    strncpy(fm.group, current_user->group, MAX_UNAME - 1);
    parse_octal_perm(o, fm.perm[0]);
    parse_octal_perm(g, fm.perm[1]);
    parse_octal_perm(ot, fm.perm[2]);
    fm.encrypted = 0;
    fm.checksum = 0;
    fm.created = fm.modified = time(NULL);
    fm.active = 1;
    files[file_count++] = fm;
    save_files();

    char modestr[10]; mode_str(&fm, modestr);
    printf("\n");
    say_ok("File '%s' created (perms %s, owner=%s, group=%s).",
           fname, modestr, fm.owner, fm.group);
    audit_log(current_user->username, "CREATE", fname, "SUCCESS");
    pause_enter();
}

/* read raw bytes off disk into a malloc'd buffer, sets *outlen */
static unsigned char *read_raw(const char *path, size_t *outlen) {
    FILE *f = fopen(path, "rb");
    if (!f) { *outlen = 0; return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); *outlen = 0; return NULL; }
    unsigned char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); *outlen = 0; return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';
    *outlen = got;
    return buf;
}

static void open_read_file(void) {
    action_header("OPEN / READ FILE");
    char fname[MAX_FNAME];
    read_line("Filename: ", fname, MAX_FNAME);
    int idx = find_file(fname);
    if (idx == -1) {
        say_err("File not found.");
        audit_log(current_user->username, "READ", fname, "FAIL-notfound");
        pause_enter(); return;
    }
    if (!check_permission(&files[idx], current_user, 'r')) {
        say_err("Permission denied: no read access.");
        audit_log(current_user->username, "READ", fname, "FAIL-denied");
        pause_enter(); return;
    }
    char path[300];
    vault_path(fname, path, sizeof(path));
    size_t len;
    unsigned char *buf = read_raw(path, &len);
    if (!buf) {
        say_err("Could not read file from disk.");
        audit_log(current_user->username, "READ", fname, "FAIL-io");
        pause_enter(); return;
    }

    if (files[idx].encrypted) {
        char pass[128];
        read_password("File is encrypted. Enter passphrase: ", pass, sizeof(pass));
        xor_crypt(buf, len, pass);
        unsigned long cs = checksum_buf(buf, len);
        if (cs != files[idx].checksum) {
            say_err("Incorrect passphrase (integrity check failed).");
            audit_log(current_user->username, "READ", fname, "FAIL-badpass");
            free(buf);
            pause_enter(); return;
        }
    }

    printf("\n");
    hline("  +", "-", "+", DIM);
    fwrite(buf, 1, len, stdout);
    if (len == 0 || buf[len-1] != '\n') printf("\n");
    hline("  +", "-", "+", DIM);
    printf(DIM "  (%zu bytes)\n\n" RESET, len);
    free(buf);
    audit_log(current_user->username, "READ", fname, "SUCCESS");
    pause_enter();
}

/* collect multi-line text input, terminated by a line containing ::END:: */
static unsigned char *collect_multiline(size_t *outlen) {
    size_t cap = 1024, len = 0;
    unsigned char *buf = malloc(cap);
    if (!buf) { *outlen = 0; return NULL; }
    char line[MAX_LINE];
    printf(DIM "  (type your content; finish with a line containing only ::END::)\n" RESET);
    while (1) {
        if (fgets(line, sizeof(line), stdin) == NULL) break;
        if (strncmp(line, "::END::", 7) == 0) break;
        size_t l = strlen(line);
        if (len + l + 1 > cap) {
            while (len + l + 1 > cap) cap *= 2;
            unsigned char *nb = realloc(buf, cap);
            if (!nb) { free(buf); *outlen = 0; return NULL; }
            buf = nb;
        }
        memcpy(buf + len, line, l);
        len += l;
    }
    *outlen = len;
    return buf;
}

static void write_file_op(void) {
    action_header("WRITE (OVERWRITE) FILE");
    char fname[MAX_FNAME];
    read_line("Filename: ", fname, MAX_FNAME);
    int idx = find_file(fname);
    if (idx == -1) {
        say_err("File not found.");
        audit_log(current_user->username, "WRITE", fname, "FAIL-notfound");
        pause_enter(); return;
    }
    if (!check_permission(&files[idx], current_user, 'w')) {
        say_err("Permission denied: no write access.");
        audit_log(current_user->username, "WRITE", fname, "FAIL-denied");
        pause_enter(); return;
    }

    size_t len;
    unsigned char *buf = collect_multiline(&len);
    if (!buf) { say_err("Memory error."); pause_enter(); return; }

    char pass[128] = {0};
    if (files[idx].encrypted) {
        read_password("Re-enter passphrase to re-encrypt on save: ", pass, sizeof(pass));
        files[idx].checksum = checksum_buf(buf, len);
        xor_crypt(buf, len, pass);
    }

    char path[300];
    vault_path(fname, path, sizeof(path));
    FILE *f = fopen(path, "wb");
    if (!f) {
        say_err("Could not write to disk.");
        free(buf);
        audit_log(current_user->username, "WRITE", fname, "FAIL-io");
        pause_enter(); return;
    }
    fwrite(buf, 1, len, f);
    fclose(f);
    free(buf);
    files[idx].modified = time(NULL);
    save_files();

    printf("\n");
    say_ok("File '%s' written (%zu bytes).", fname, len);
    audit_log(current_user->username, "WRITE", fname, "SUCCESS");
    pause_enter();
}

static void append_file_op(void) {
    action_header("APPEND TO FILE");
    char fname[MAX_FNAME];
    read_line("Filename: ", fname, MAX_FNAME);
    int idx = find_file(fname);
    if (idx == -1) {
        say_err("File not found.");
        audit_log(current_user->username, "APPEND", fname, "FAIL-notfound");
        pause_enter(); return;
    }
    if (!check_permission(&files[idx], current_user, 'w')) {
        say_err("Permission denied: no write access.");
        audit_log(current_user->username, "APPEND", fname, "FAIL-denied");
        pause_enter(); return;
    }

    char path[300];
    vault_path(fname, path, sizeof(path));

    if (files[idx].encrypted) {
        char pass[128];
        read_password("Enter passphrase to unlock file: ", pass, sizeof(pass));
        size_t oldlen;
        unsigned char *oldbuf = read_raw(path, &oldlen);
        if (!oldbuf) { say_err("IO error."); pause_enter(); return; }
        xor_crypt(oldbuf, oldlen, pass);
        if (checksum_buf(oldbuf, oldlen) != files[idx].checksum) {
            say_err("Incorrect passphrase.");
            free(oldbuf);
            audit_log(current_user->username, "APPEND", fname, "FAIL-badpass");
            pause_enter(); return;
        }
        size_t addlen;
        unsigned char *addbuf = collect_multiline(&addlen);
        size_t total = oldlen + addlen;
        unsigned char *merged = malloc(total);
        memcpy(merged, oldbuf, oldlen);
        memcpy(merged + oldlen, addbuf, addlen);
        free(oldbuf); free(addbuf);

        files[idx].checksum = checksum_buf(merged, total);
        xor_crypt(merged, total, pass);
        FILE *f = fopen(path, "wb");
        if (f) { fwrite(merged, 1, total, f); fclose(f); }
        free(merged);
    } else {
        size_t addlen;
        unsigned char *addbuf = collect_multiline(&addlen);
        FILE *f = fopen(path, "ab");
        if (!f) {
            say_err("Could not open file for append.");
            free(addbuf);
            audit_log(current_user->username, "APPEND", fname, "FAIL-io");
            pause_enter(); return;
        }
        fwrite(addbuf, 1, addlen, f);
        fclose(f);
        free(addbuf);
    }

    files[idx].modified = time(NULL);
    save_files();
    printf("\n");
    say_ok("Content appended to '%s'.", fname);
    audit_log(current_user->username, "APPEND", fname, "SUCCESS");
    pause_enter();
}

static void delete_file_op(void) {
    action_header("DELETE FILE");
    char fname[MAX_FNAME];
    read_line("Filename: ", fname, MAX_FNAME);
    int idx = find_file(fname);
    if (idx == -1) {
        say_err("File not found.");
        audit_log(current_user->username, "DELETE", fname, "FAIL-notfound");
        pause_enter(); return;
    }
    if (!(current_user->is_admin || strcmp(files[idx].owner, current_user->username) == 0
          || check_permission(&files[idx], current_user, 'w'))) {
        say_err("Permission denied: only the owner, admin, or a user with write access may delete.");
        audit_log(current_user->username, "DELETE", fname, "FAIL-denied");
        pause_enter(); return;
    }
    char confirm[8];
    read_line("Type 'yes' to confirm deletion: ", confirm, sizeof(confirm));
    if (strcmp(confirm, "yes") != 0) {
        say_warn("Cancelled.");
        pause_enter(); return;
    }
    char path[300];
    vault_path(fname, path, sizeof(path));
    remove(path);

    for (int i = idx; i < file_count - 1; i++) files[i] = files[i+1];
    file_count--;
    save_files();

    printf("\n");
    say_ok("File '%s' deleted.", fname);
    audit_log(current_user->username, "DELETE", fname, "SUCCESS");
    pause_enter();
}

static void rename_file_op(void) {
    action_header("RENAME FILE");
    char fname[MAX_FNAME], newname[MAX_FNAME];
    read_line("Current filename: ", fname, MAX_FNAME);
    int idx = find_file(fname);
    if (idx == -1) {
        say_err("File not found.");
        audit_log(current_user->username, "RENAME", fname, "FAIL-notfound");
        pause_enter(); return;
    }
    if (!check_permission(&files[idx], current_user, 'w')) {
        say_err("Permission denied: no write access.");
        audit_log(current_user->username, "RENAME", fname, "FAIL-denied");
        pause_enter(); return;
    }
    read_line("New filename: ", newname, MAX_FNAME);
    if (!valid_filename(newname) || find_file(newname) != -1) {
        say_err("Invalid or already-used new filename.");
        audit_log(current_user->username, "RENAME", fname, "FAIL-badname");
        pause_enter(); return;
    }
    char oldpath[300], newpath[300];
    vault_path(fname, oldpath, sizeof(oldpath));
    vault_path(newname, newpath, sizeof(newpath));
    if (rename(oldpath, newpath) != 0) {
        say_err("Rename failed on disk.");
        audit_log(current_user->username, "RENAME", fname, "FAIL-io");
        pause_enter(); return;
    }
    strncpy(files[idx].filename, newname, MAX_FNAME - 1);
    files[idx].modified = time(NULL);
    save_files();
    printf("\n");
    say_ok("Renamed '%s' -> '%s'.", fname, newname);
    char detail[300]; snprintf(detail, sizeof(detail), "%s->%s", fname, newname);
    audit_log(current_user->username, "RENAME", detail, "SUCCESS");
    pause_enter();
}

/* ----------------------------------------------------------------------
   Simplified permission editor:
     - Shows the current permissions in plain English (not octal).
     - Offers common presets first (private / share with group / public).
     - Offers a step-by-step "custom" mode that just asks yes/no questions
       for Owner/Group/Other x Read/Write/Execute, defaulting to whatever
       is already set, so nothing needs to be memorised.
   ---------------------------------------------------------------------- */
static void chmod_file_op(void) {
    action_header("CHANGE PERMISSIONS");
    char fname[MAX_FNAME];
    read_line("Filename: ", fname, MAX_FNAME);
    int idx = find_file(fname);
    if (idx == -1) {
        say_err("File not found.");
        pause_enter(); return;
    }
    if (!(current_user->is_admin || strcmp(files[idx].owner, current_user->username) == 0)) {
        say_err("Only the owner or admin may change permissions.");
        audit_log(current_user->username, "CHMOD", fname, "FAIL-denied");
        pause_enter(); return;
    }

    char cur[10]; mode_str(&files[idx], cur);
    say_info("Current permissions: %s  (owner/group/other)", cur);

    printf("\n");
    MenuItem presets[] = {
        {1, "Private (only I can read/write)"},
        {2, "Share with my group (they can read)"},
        {3, "Share with my group (they can read+write)"},
        {4, "Public read-only (everyone can read)"},
        {5, "Custom (choose read/write/execute one by one)"},
        {0, "Cancel"}
    };
    print_menu_two_col(presets, 6);
    printf("\n");
    char choice[8];
    read_line("Choose: ", choice, sizeof(choice));
    int c = atoi(choice);

    switch (c) {
        case 1: /* rw-,---,--- */
            parse_octal_perm(6, files[idx].perm[0]);
            parse_octal_perm(0, files[idx].perm[1]);
            parse_octal_perm(0, files[idx].perm[2]);
            break;
        case 2: /* rw-,r--,--- */
            parse_octal_perm(6, files[idx].perm[0]);
            parse_octal_perm(4, files[idx].perm[1]);
            parse_octal_perm(0, files[idx].perm[2]);
            break;
        case 3: /* rw-,rw-,--- */
            parse_octal_perm(6, files[idx].perm[0]);
            parse_octal_perm(6, files[idx].perm[1]);
            parse_octal_perm(0, files[idx].perm[2]);
            break;
        case 4: /* rw-,r--,r-- */
            parse_octal_perm(6, files[idx].perm[0]);
            parse_octal_perm(4, files[idx].perm[1]);
            parse_octal_perm(4, files[idx].perm[2]);
            break;
        case 5:
            chmod_interactive(&files[idx]);
            break;
        case 0:
        default:
            say_warn("Cancelled.");
            pause_enter();
            return;
    }

    files[idx].modified = time(NULL);
    save_files();
    char newmode[10]; mode_str(&files[idx], newmode);
    printf("\n");
    say_ok("Permissions for '%s' set to %s.", fname, newmode);
    audit_log(current_user->username, "CHMOD", fname, "SUCCESS");
    pause_enter();
}

/* ============================================================================
   ENCRYPTION / DECRYPTION
   ============================================================================ */
static void encrypt_file_op(void) {
    action_header("ENCRYPT FILE");
    char fname[MAX_FNAME];
    read_line("Filename: ", fname, MAX_FNAME);
    int idx = find_file(fname);
    if (idx == -1) {
        say_err("File not found.");
        pause_enter(); return;
    }
    if (!check_permission(&files[idx], current_user, 'w')) {
        say_err("Permission denied: no write access.");
        audit_log(current_user->username, "ENCRYPT", fname, "FAIL-denied");
        pause_enter(); return;
    }
    if (files[idx].encrypted) {
        say_warn("File is already encrypted.");
        pause_enter(); return;
    }
    char pass1[128], pass2[128];
    read_password("Set encryption passphrase: ", pass1, sizeof(pass1));
    read_password("Confirm passphrase: ", pass2, sizeof(pass2));
    if (strcmp(pass1, pass2) != 0 || strlen(pass1) == 0) {
        say_err("Passphrases did not match.");
        pause_enter(); return;
    }
    char path[300];
    vault_path(fname, path, sizeof(path));
    size_t len;
    unsigned char *buf = read_raw(path, &len);
    if (!buf) { say_err("IO error."); pause_enter(); return; }

    files[idx].checksum = checksum_buf(buf, len);
    xor_crypt(buf, len, pass1);

    FILE *f = fopen(path, "wb");
    fwrite(buf, 1, len, f);
    fclose(f);
    free(buf);

    files[idx].encrypted = 1;
    files[idx].modified = time(NULL);
    save_files();
    printf("\n");
    say_ok("'%s' is now encrypted.", fname);
    say_warn("Remember your passphrase -- it is NOT stored anywhere!");
    audit_log(current_user->username, "ENCRYPT", fname, "SUCCESS");
    pause_enter();
}

static void decrypt_file_op(void) {
    action_header("DECRYPT FILE");
    char fname[MAX_FNAME];
    read_line("Filename: ", fname, MAX_FNAME);
    int idx = find_file(fname);
    if (idx == -1) {
        say_err("File not found.");
        pause_enter(); return;
    }
    if (!files[idx].encrypted) {
        say_warn("File is not encrypted.");
        pause_enter(); return;
    }
    if (!check_permission(&files[idx], current_user, 'w')) {
        say_err("Permission denied: no write access.");
        audit_log(current_user->username, "DECRYPT", fname, "FAIL-denied");
        pause_enter(); return;
    }
    char pass[128];
    read_password("Enter passphrase: ", pass, sizeof(pass));
    char path[300];
    vault_path(fname, path, sizeof(path));
    size_t len;
    unsigned char *buf = read_raw(path, &len);
    if (!buf) { say_err("IO error."); pause_enter(); return; }

    xor_crypt(buf, len, pass);
    if (checksum_buf(buf, len) != files[idx].checksum) {
        say_err("Incorrect passphrase or corrupted data -- aborting (file left encrypted).");
        free(buf);
        audit_log(current_user->username, "DECRYPT", fname, "FAIL-badpass");
        pause_enter(); return;
    }
    FILE *f = fopen(path, "wb");
    fwrite(buf, 1, len, f);
    fclose(f);
    free(buf);

    files[idx].encrypted = 0;
    files[idx].checksum = 0;
    files[idx].modified = time(NULL);
    save_files();
    printf("\n");
    say_ok("'%s' decrypted and stored as plaintext.", fname);
    audit_log(current_user->username, "DECRYPT", fname, "SUCCESS");
    pause_enter();
}

/* ============================================================================
   ADMIN TOOLS
   ============================================================================ */
static void view_audit_log(void) {
    action_header(current_user->is_admin ? "AUDIT LOG (full)" : "AUDIT LOG (your activity)");
    FILE *f = fopen(AUDIT_LOG, "r");
    if (!f) { printf(DIM "  (no audit entries yet)\n\n" RESET); pause_enter(); return; }
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (!current_user->is_admin) {
            if (strstr(line, current_user->username) == NULL) continue;
        }
        if (strstr(line, "SUCCESS")) printf(GREEN "  %s" RESET, line);
        else if (strstr(line, "FAIL")) printf(RED "  %s" RESET, line);
        else printf("  %s", line);
    }
    fclose(f);
    printf("\n");
    pause_enter();
}

static void manage_users(void) {
    while (1) {
        action_header("USER MANAGEMENT (admin)");
        MenuItem items[] = {
            {1, "List users"}, {2, "Create user"},
            {3, "Delete user"}, {4, "Toggle admin flag"},
            {5, "Reset user's password"}, {0, "Back"}
        };
        print_menu_two_col(items, 6);
        printf("\n");
        char choice[8];
        read_line("Choose: ", choice, sizeof(choice));
        int c = atoi(choice);
        if (c == 0) return;
        if (c == 1) {
            printf("\n  %s%-16s %-12s %-8s%s\n", BOLD, "USERNAME", "GROUP", "ADMIN", RESET);
            hline("  +", "-", "+", DIM);
            for (int i = 0; i < user_count; i++) {
                if (!users[i].active) continue;
                printf("  %-16s %-12s %-8s\n", users[i].username, users[i].group,
                       users[i].is_admin ? "yes" : "no");
            }
            printf("\n");
            pause_enter();
        } else if (c == 2) {
            register_user();
        } else if (c == 3) {
            char uname[MAX_UNAME];
            read_line("Username to delete: ", uname, MAX_UNAME);
            int idx = find_user(uname);
            if (idx == -1) { say_err("Not found."); pause_enter(); continue; }
            if (strcmp(uname, current_user->username) == 0) {
                say_err("Cannot delete yourself."); pause_enter(); continue;
            }
            users[idx].active = 0;
            save_users();
            say_ok("User '%s' deactivated.", uname);
            audit_log(current_user->username, "DEL_USER", uname, "SUCCESS");
            pause_enter();
        } else if (c == 4) {
            char uname[MAX_UNAME];
            read_line("Username: ", uname, MAX_UNAME);
            int idx = find_user(uname);
            if (idx == -1) { say_err("Not found."); pause_enter(); continue; }
            users[idx].is_admin = !users[idx].is_admin;
            save_users();
            say_ok("Admin flag for '%s' is now %s.", uname, users[idx].is_admin ? "ON" : "OFF");
            audit_log(current_user->username, "TOGGLE_ADMIN", uname, "SUCCESS");
            pause_enter();
        } else if (c == 5) {
            char uname[MAX_UNAME], np1[128], np2[128];
            read_line("Username: ", uname, MAX_UNAME);
            int idx = find_user(uname);
            if (idx == -1) { say_err("Not found."); pause_enter(); continue; }
            read_password("New password: ", np1, sizeof(np1));
            read_password("Confirm: ", np2, sizeof(np2));
            if (strcmp(np1, np2) != 0 || strlen(np1) < 4) {
                say_err("Mismatch or too short."); pause_enter(); continue;
            }
            gen_salt(users[idx].salt);
            hash_password(np1, users[idx].salt, users[idx].passhash);
            save_users();
            say_ok("Password reset for '%s'.", uname);
            audit_log(current_user->username, "RESET_PW", uname, "SUCCESS");
            pause_enter();
        }
    }
}

/* ============================================================================
   MAIN MENU
   ============================================================================ */
static void main_menu(void) {
    while (1) {
        clear_screen();
        banner();
        printf("\n");
        status_bar();

        section_rule("FILE OPERATIONS", BBLUE);
        MenuItem file_ops[] = {
            {1, "List files"},          {5, "Delete file"},
            {2, "Create file"},         {6, "Rename file"},
            {3, "Open / Read file"},    {7, "Change permissions (chmod)"},
            {4, "Write (overwrite)"},   {8, "Append to file"},
        };
        print_menu_two_col(file_ops, 8);

        section_rule("SECURITY", BMAGENTA);
        MenuItem sec_ops[5];
        int n = 0;
        sec_ops[n++] = (MenuItem){9, "Encrypt file"};
        sec_ops[n++] = (MenuItem){10, "Decrypt file"};
        sec_ops[n++] = (MenuItem){11, "View audit log"};
        sec_ops[n++] = (MenuItem){12, "Change my password"};
        if (current_user->is_admin) sec_ops[n++] = (MenuItem){13, "Manage users (admin)"};
        print_menu_two_col(sec_ops, n);

        printf("\n");
        hline("  +", "-", "+", DIM);
        printf("    " BOLD "0) Logout" RESET "\n");
        hline("  +", "-", "+", DIM);

        char choice[8];
        read_line("\nSelect an option: ", choice, sizeof(choice));
        int c = atoi(choice);
        switch (c) {
            case 1: list_files(); break;
            case 2: create_file_op(); break;
            case 3: open_read_file(); break;
            case 4: write_file_op(); break;
            case 5: delete_file_op(); break;
            case 6: rename_file_op(); break;
            case 7: chmod_file_op(); break;
            case 8: append_file_op(); break;
            case 9: encrypt_file_op(); break;
            case 10: decrypt_file_op(); break;
            case 11: view_audit_log(); break;
            case 12:
                action_header("CHANGE PASSWORD");
                change_password();
                printf("\n");
                pause_enter();
                break;
            case 13:
                if (current_user->is_admin) manage_users();
                break;
            case 0:
                audit_log(current_user->username, "LOGOUT", "-", "SUCCESS");
                current_user = NULL;
                return;
            default:
                say_err("Invalid option.");
                pause_enter();
        }
    }
}

/* ============================================================================
   ENTRY POINT
   ============================================================================ */
int main(void) {
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    ensure_vault_dir();
    load_users();
    load_files();
    create_default_admin();

    while (1) {
        clear_screen();
        banner();
        printf("\n");
        MenuItem items[] = { {1, "Login"}, {2, "Register new account"}, {0, "Exit"} };
        print_menu_two_col(items, 3);
        char choice[8];
        read_line("\nSelect an option: ", choice, sizeof(choice));
        int c = atoi(choice);
        if (c == 1) {
            if (login()) {
                pause_enter();
                main_menu();
            } else {
                pause_enter();
            }
        } else if (c == 2) {
            register_user();
        } else if (c == 0) {
            clear_screen();
            printf(BCYAN "  Goodbye!\n" RESET);
            break;
        } else {
            say_err("Invalid option.");
            pause_enter();
        }
    }
    return 0;
}
