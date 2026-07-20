/* ============================================================================
 * FILE: server.c
 * Multi-threaded TCP server demonstrating IPC over sockets, a simple text
 * protocol, concurrent client handling, authentication with hashed
 * passwords, input validation, and robust error/connection handling.
 *
 * Structure of this file:
 *   PART B: User database (registration / login / password hashing)
 *   PART C: Per-client connection handler (protocol parsing + dispatch)
 *   PART D: Server bootstrap (socket setup + accept loop + concurrency)
 *
 * Build: see Makefile ("make")  ->  requires -lpthread -lcrypt
 * Run:   ./server
 * ==========================================================================*/

#define _GNU_SOURCE               /* needed for crypt_r() thread-safe hashing */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <signal.h>
#include <crypt.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common.h"

/* ---------------------------------------------------------------------------
 * Global state
 * -------------------------------------------------------------------------*/
static int listen_fd = -1;                         /* listening socket        */
static pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER; /* protects users.db */
static sem_t client_slots;                          /* caps concurrent clients */

/* Per-connection context passed to each worker thread. */
typedef struct {
    int  fd;
    char client_ip[INET_ADDRSTRLEN];
    int  client_port;
} client_ctx_t;

/* ============================================================================
 * PART B: USER DATABASE  (Registration + Login with password hashing)
 * ----------------------------------------------------------------------------
 * Users are stored one-per-line in USER_DB_FILE as:
 *      username:hash
 * The password itself is NEVER stored - only a salted SHA-512 hash produced
 * by crypt_r(), the thread-safe variant of the standard POSIX crypt().
 * A mutex serialises all reads/writes to the file since multiple client
 * threads may register/login at the same time.
 * ==========================================================================*/

/* ---- B.1 Input validation ------------------------------------------------
 * Rejects malformed usernames/passwords before they ever touch the file or
 * the hashing routine. This is our "basic data validation" security measure.
 * -------------------------------------------------------------------------*/
static int validate_username(const char *u) {
    size_t len = strlen(u);
    if (len < MIN_USERNAME_LEN || len >= MAX_USERNAME_LEN) return 0;
    for (size_t i = 0; i < len; i++) {
        /* Only allow alphanumerics and underscore; ':' is our DB delimiter
         * and must never be permitted inside a username. */
        if (!isalnum((unsigned char)u[i]) && u[i] != '_') return 0;
    }
    return 1;
}

static int validate_password(const char *p) {
    size_t len = strlen(p);
    if (len < MIN_PASSWORD_LEN || len >= MAX_PASSWORD_LEN) return 0;
    for (size_t i = 0; i < len; i++) {
        if (isspace((unsigned char)p[i]) || iscntrl((unsigned char)p[i])) return 0;
    }
    return 1;
}

/* ---- B.2 Salt generation --------------------------------------------------
 * Builds a random SHA-512 salt string in the "$6$xxxxxxxx$" crypt(3) format
 * using bytes from /dev/urandom for unpredictability.
 * -------------------------------------------------------------------------*/
static void generate_salt(char *out, size_t out_size) {
    static const char alphabet[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./";
    unsigned char rnd[16];

    FILE *urandom = fopen("/dev/urandom", "rb");
    if (urandom) {
        if (fread(rnd, 1, sizeof(rnd), urandom) != sizeof(rnd)) {
            /* fallback below if short read */
        }
        fclose(urandom);
    } else {
        for (size_t i = 0; i < sizeof(rnd); i++) rnd[i] = (unsigned char)rand();
    }

    snprintf(out, out_size, "$6$");                 /* $6$ => SHA-512 method  */
    size_t prefix_len = strlen(out);
    for (size_t i = 0; i < 16 && prefix_len + i + 1 < out_size; i++) {
        out[prefix_len + i] = alphabet[rnd[i] % (sizeof(alphabet) - 1)];
    }
    out[prefix_len + 16] = '$';
    out[prefix_len + 17] = '\0';
}

/* ---- B.3 Hash a password with a (new or existing) salt -------------------*/
static const char *hash_password(const char *password, const char *salt,
                                  struct crypt_data *cdata) {
    cdata->initialized = 0;
    return crypt_r(password, salt, cdata);
}

/* ---- B.4 Look up a username in users.db -----------------------------------
 * Returns 1 and copies the stored hash into hash_out if found, else 0.
 * Caller must already hold db_mutex.
 * -------------------------------------------------------------------------*/
static int lookup_user(const char *username, char *hash_out, size_t hash_out_size) {
    FILE *fp = fopen(USER_DB_FILE, "r");
    if (!fp) return 0;                    /* no DB file yet => no users      */

    char line[LINE_BUF_SIZE];
    int found = 0;
    while (fgets(line, sizeof(line), fp)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *sep = strchr(line, ':');
        if (!sep) continue;               /* skip malformed lines            */
        *sep = '\0';
        const char *stored_user = line;
        const char *stored_hash = sep + 1;
        if (strcmp(stored_user, username) == 0) {
            strncpy(hash_out, stored_hash, hash_out_size - 1);
            hash_out[hash_out_size - 1] = '\0';
            found = 1;
            break;
        }
    }
    fclose(fp);
    return found;
}

/* ---- B.5 Register a new user ----------------------------------------------
 * Returns: 0 = success, 1 = invalid input, 2 = username already exists,
 *          3 = internal/file error
 * -------------------------------------------------------------------------*/
static int register_user(const char *username, const char *password) {
    if (!validate_username(username) || !validate_password(password)) return 1;

    pthread_mutex_lock(&db_mutex);

    char existing_hash[256];
    if (lookup_user(username, existing_hash, sizeof(existing_hash))) {
        pthread_mutex_unlock(&db_mutex);
        return 2;                          /* duplicate username             */
    }

    char salt[32];
    generate_salt(salt, sizeof(salt));

    struct crypt_data cdata;
    const char *hash = hash_password(password, salt, &cdata);
    if (!hash) {
        pthread_mutex_unlock(&db_mutex);
        return 3;
    }

    FILE *fp = fopen(USER_DB_FILE, "a");
    if (!fp) {
        pthread_mutex_unlock(&db_mutex);
        return 3;
    }
    fprintf(fp, "%s:%s\n", username, hash);
    fclose(fp);

    pthread_mutex_unlock(&db_mutex);
    return 0;
}

/* ---- B.6 Authenticate an existing user ------------------------------------
 * Returns: 0 = success, 1 = invalid input, 2 = user not found,
 *          3 = wrong password
 * -------------------------------------------------------------------------*/
static int authenticate_user(const char *username, const char *password) {
    if (!validate_username(username) || !validate_password(password)) return 1;

    pthread_mutex_lock(&db_mutex);
    char stored_hash[256];
    if (!lookup_user(username, stored_hash, sizeof(stored_hash))) {
        pthread_mutex_unlock(&db_mutex);
        return 2;                          /* no such user                   */
    }
    pthread_mutex_unlock(&db_mutex);       /* hashing is CPU work; no need
                                               to hold the lock for it        */

    struct crypt_data cdata;
    const char *computed = hash_password(password, stored_hash, &cdata);
    if (!computed || strcmp(computed, stored_hash) != 0) return 3;
    return 0;
}

/* ============================================================================
 * PART C: PER-CLIENT CONNECTION HANDLER
 * ----------------------------------------------------------------------------
 * Each accepted connection is served by its own detached pthread, allowing
 * the server to handle many clients concurrently. This function:
 *   1. Reads protocol lines from the socket (handles partial TCP reads)
 *   2. Parses and validates each command
 *   3. Dispatches to the registration/login/data-exchange logic
 *   4. Sends back an OK/ERR response
 *   5. Handles disconnects and I/O errors gracefully
 * ==========================================================================*/

/* ---- C.1 Reliable line reader ---------------------------------------------
 * Reads one '\n'-terminated line from the socket into buf (NUL-terminated,
 * newline stripped). Returns line length on success, 0 on clean disconnect,
 * -1 on error, -2 if the client sent an oversized line (protocol abuse).
 * -------------------------------------------------------------------------*/
static ssize_t read_line(int fd, char *buf, size_t buf_size) {
    size_t total = 0;
    while (total + 1 < buf_size) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n == 0) {
            return (total == 0) ? 0 : (ssize_t)total; /* peer closed         */
        }
        if (n < 0) {
            if (errno == EINTR) continue;   /* interrupted syscall, retry    */
            return -1;                      /* real error                   */
        }
        if (c == '\n') {
            buf[total] = '\0';
            return (ssize_t)total;
        }
        if (c != '\r') buf[total++] = c;    /* tolerate CRLF line endings    */
    }
    return -2;                              /* line too long                */
}

/* ---- C.2 Reliable send (loops until all bytes are written) ---------------*/
static int send_all(int fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

/* Convenience wrapper: send "OK <fmt>\n" or "ERR <fmt>\n" */
static void reply(int fd, const char *status, const char *msg) {
    char out[LINE_BUF_SIZE];
    int len = snprintf(out, sizeof(out), "%s %s\n", status, msg);
    if (len > 0) send_all(fd, out, (size_t)len);
}

/* ---- C.3 The worker thread function ---------------------------------------
 * One instance of this runs per connected client.
 * -------------------------------------------------------------------------*/
static void *handle_client(void *arg) {
    client_ctx_t *ctx = (client_ctx_t *)arg;
    int fd = ctx->fd;
    char peer_desc[64];
    snprintf(peer_desc, sizeof(peer_desc), "%s:%d", ctx->client_ip, ctx->client_port);

    printf("[+] Client connected: %s\n", peer_desc);

    int   authenticated = 0;
    char  current_user[MAX_USERNAME_LEN] = {0};
    char  line[LINE_BUF_SIZE];

    for (;;) {
        ssize_t n = read_line(fd, line, sizeof(line));
        if (n == 0) {                       /* client disconnected cleanly   */
            printf("[-] Client disconnected: %s\n", peer_desc);
            break;
        }
        if (n == -1) {                      /* socket error                  */
            fprintf(stderr, "[!] recv error from %s: %s\n", peer_desc, strerror(errno));
            break;
        }
        if (n == -2) {                      /* oversized/garbage input       */
            reply(fd, RESP_ERR, "line too long");
            continue;
        }

        char *cmd  = strtok(line, " ");
        char *rest = strtok(NULL, "");      /* everything after first space  */
        if (!cmd) {
            reply(fd, RESP_ERR, "empty command");
            continue;
        }
        if (strcasecmp(cmd, CMD_REGISTER) == 0) {
            char user[MAX_USERNAME_LEN] = {0}, pass[MAX_PASSWORD_LEN] = {0};
            if (!rest || sscanf(rest, "%31s %63s", user, pass) != 2) {
                reply(fd, RESP_ERR, "usage: REGISTER <username> <password>");
                continue;
            }
            int rc = register_user(user, pass);
            switch (rc) {
                case 0: reply(fd, RESP_OK, "registration successful"); break;
                case 1: reply(fd, RESP_ERR, "invalid username/password format"); break;
                case 2: reply(fd, RESP_ERR, "username already exists"); break;
                default: reply(fd, RESP_ERR, "server error during registration"); break;
            }

        } else if (strcasecmp(cmd, CMD_LOGIN) == 0) {
            char user[MAX_USERNAME_LEN] = {0}, pass[MAX_PASSWORD_LEN] = {0};
            if (!rest || sscanf(rest, "%31s %63s", user, pass) != 2) {
                reply(fd, RESP_ERR, "usage: LOGIN <username> <password>");
                continue;
            }
            int rc = authenticate_user(user, pass);
            if (rc == 0) {
                authenticated = 1;
                strncpy(current_user, user, sizeof(current_user) - 1);
                reply(fd, RESP_OK, "login successful");
                printf("[i] %s authenticated as '%s'\n", peer_desc, current_user);
            } else if (rc == 1) {
                reply(fd, RESP_ERR, "invalid username/password format");
            } else if (rc == 2) {
                reply(fd, RESP_ERR, "no such user");
            } else {
                reply(fd, RESP_ERR, "incorrect password");
            }

        } else if (strcasecmp(cmd, CMD_MSG) == 0) {
            if (!authenticated) {
                reply(fd, RESP_ERR, "you must LOGIN before sending data");
                continue;
            }
            if (!rest || strlen(rest) == 0) {
                reply(fd, RESP_ERR, "usage: MSG <text>");
                continue;
            }
            /* --- Example "data exchange": server processes the payload
             * (uppercases it) and echoes it back, proving two-way exchange. */
            char processed[LINE_BUF_SIZE];
            size_t i = 0;
            for (; rest[i] != '\0' && i < sizeof(processed) - 1; i++)
                processed[i] = (char)toupper((unsigned char)rest[i]);
            processed[i] = '\0';

            char out[LINE_BUF_SIZE + 32];
            snprintf(out, sizeof(out), "%s ECHO: %s\n", RESP_OK, processed);
            send_all(fd, out, strlen(out));
            printf("[i] MSG from '%s': %s\n", current_user, rest);

        } else if (strcasecmp(cmd, CMD_TIME) == 0) {
            if (!authenticated) {
                reply(fd, RESP_ERR, "you must LOGIN before requesting data");
                continue;
            }
            time_t now = time(NULL);
            char tbuf[64];
            struct tm tmv;
            localtime_r(&now, &tmv);
            strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tmv);
            reply(fd, RESP_OK, tbuf);

        } else if (strcasecmp(cmd, CMD_QUIT) == 0) {
            reply(fd, RESP_OK, "bye");
            break;

        } else {
            reply(fd, RESP_ERR, "unknown command");
        }
    }

    /* ---- Cleanup: always executed, even on error paths -------------------*/
    close(fd);
    sem_post(&client_slots);       /* free up a concurrency slot            */
    free(ctx);
    return NULL;
}

/* ============================================================================
 * PART D: SERVER BOOTSTRAP (socket setup + accept loop)
 * ----------------------------------------------------------------------------
 * Creates the listening TCP socket, binds it, and loops accepting new
 * connections, handing each one off to a dedicated worker thread. A
 * semaphore bounds MAX_CONCURRENT_CLIENTS to protect server resources.
 * A SIGINT handler allows a clean shutdown (closes the listening socket).
 * ==========================================================================*/

static void handle_sigint(int signo) {
    (void)signo;
    printf("\n[i] Shutting down server...\n");
    if (listen_fd >= 0) close(listen_fd);
    exit(0);
}

int main(void) {
    /* Ignore SIGPIPE so that writing to a closed socket returns an EPIPE
     * error instead of killing the whole process - key error-handling step
     * for a long-running network server. */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, handle_sigint);

    /* Line-buffer stdout so log messages appear immediately (important when
     * output is redirected to a file/pipe rather than a terminal). */
    setvbuf(stdout, NULL, _IOLBF, 0);

    sem_init(&client_slots, 0, MAX_CONCURRENT_CLIENTS);

    /* ---- D.1 Create socket ------------------------------------------------*/
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    /* Allow immediate re-binding to the port after restart (avoids
     * "Address already in use" during development). */
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    /* ---- D.2 Bind ----------------------------------------------------------*/
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(SERVER_PORT);

    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    /* ---- D.3 Listen ----------------------------------------------------------*/
    if (listen(listen_fd, LISTEN_BACKLOG) < 0) {
        perror("listen");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    printf("=== IPC Server ===\n");
    printf("Listening on port %d ... (Ctrl+C to stop)\n", SERVER_PORT);

    /* ---- D.4 Accept loop -----------------------------------------------------
     * Blocks on accept() waiting for new TCP connections. Each connection is
     * bounded by the client_slots semaphore, then dispatched to a detached
     * worker thread so the accept loop can immediately serve the next
     * client (this is how the server supports multiple *concurrent*
     * connections rather than serving clients one at a time).
     * --------------------------------------------------------------------- */
    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;        /* interrupted, retry      */
            perror("accept");
            continue;                            /* keep serving others     */
        }

        /* Cap concurrency: if we're at the limit, sem_wait blocks until a
         * slot frees up (a finished client calls sem_post). This bounds
         * server resource usage under heavy connection load. */
        sem_wait(&client_slots);

        client_ctx_t *ctx = malloc(sizeof(client_ctx_t));
        if (!ctx) {
            fprintf(stderr, "[!] malloc failed, dropping connection\n");
            close(client_fd);
            sem_post(&client_slots);
            continue;
        }
        ctx->fd = client_fd;
        inet_ntop(AF_INET, &client_addr.sin_addr, ctx->client_ip, sizeof(ctx->client_ip));
        ctx->client_port = ntohs(client_addr.sin_port);

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, ctx) != 0) {
            perror("pthread_create");
            close(client_fd);
            free(ctx);
            sem_post(&client_slots);
            continue;
        }
        pthread_detach(tid);   /* thread cleans up its own resources on exit */
    }

    close(listen_fd);
    return 0;
}
