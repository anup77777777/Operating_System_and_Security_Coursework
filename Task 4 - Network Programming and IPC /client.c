/* ============================================================================
 * FILE: client.c
 * PART E: CLIENT APPLICATION
 * ----------------------------------------------------------------------------
 * Connects to the server over TCP, then offers an interactive menu to:
 *   1) Register a new account
 *   2) Log in
 *   3) Send a data message (protocol: MSG <text>) once authenticated
 *   4) Ask the server for its current time (protocol: TIME)
 *   5) Quit (protocol: QUIT)
 *
 * Demonstrates: socket creation/connect, sending protocol requests,
 * receiving and parsing responses, and clean error/connection handling
 * (e.g. server unreachable, connection dropped mid-session).
 * ==========================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common.h"

/* ---- E.1 Reliable line reader (mirrors the server's read_line) -----------
 * Reads one '\n'-terminated response line from the socket.
 * Returns length on success, 0 on server disconnect, -1 on error.
 * -------------------------------------------------------------------------*/
static ssize_t read_line(int fd, char *buf, size_t buf_size) {
    size_t total = 0;
    while (total + 1 < buf_size) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n == 0) return (total == 0) ? 0 : (ssize_t)total;
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (c == '\n') { buf[total] = '\0'; return (ssize_t)total; }
        if (c != '\r') buf[total++] = c;
    }
    buf[buf_size - 1] = '\0';
    return (ssize_t)total;
}

/* ---- E.2 Reliable send -----------------------------------------------------*/
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

/* ---- E.3 Send one protocol line and print the server's reply -------------
 * Returns 1 if server said OK, 0 if it said ERR, -1 on connection error.
 * -------------------------------------------------------------------------*/
static int send_command(int fd, const char *line) {
    char out[LINE_BUF_SIZE];
    int len = snprintf(out, sizeof(out), "%s\n", line);
    if (len < 0 || send_all(fd, out, (size_t)len) < 0) {
        fprintf(stderr, "[!] Failed to send command: %s\n", strerror(errno));
        return -1;
    }

    char reply[LINE_BUF_SIZE];
    ssize_t n = read_line(fd, reply, sizeof(reply));
    if (n == 0) {
        fprintf(stderr, "[!] Server closed the connection.\n");
        return -1;
    }
    if (n < 0) {
        fprintf(stderr, "[!] Error reading server response: %s\n", strerror(errno));
        return -1;
    }

    printf("Server: %s\n", reply);
    return (strncmp(reply, RESP_OK, strlen(RESP_OK)) == 0) ? 1 : 0;
}

/* ---- E.4 Small helper to read a line of input safely from the user -------*/
static void prompt_line(const char *label, char *buf, size_t buf_size) {
    printf("%s", label);
    fflush(stdout);
    if (fgets(buf, (int)buf_size, stdin) == NULL) {
        buf[0] = '\0';
        return;
    }
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
}

/* ---- E.5 Main client logic --------------------------------------------------*/
int main(int argc, char *argv[]) {
    const char *server_ip = (argc > 1) ? argv[1] : "127.0.0.1";
    int server_port = (argc > 2) ? atoi(argv[2]) : SERVER_PORT;

    /* --- Create socket ---------------------------------------------------*/
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons((uint16_t)server_port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "[!] Invalid server address: %s\n", server_ip);
        close(sock_fd);
        return EXIT_FAILURE;
    }

    /* --- Connect, with a clear error message on failure -------------------*/
    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "[!] Could not connect to %s:%d - %s\n",
                server_ip, server_port, strerror(errno));
        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("=== Connected to server %s:%d ===\n", server_ip, server_port);

    int authenticated = 0;
    char username[MAX_USERNAME_LEN] = {0};
    char line[LINE_BUF_SIZE];
    int running = 1;

    while (running) {
        printf("\n----------------------------------\n");
        printf("Logged in as: %s\n", authenticated ? username : "(not logged in)");
        printf("1) Register\n");
        printf("2) Login\n");
        printf("3) Send message (requires login)\n");
        printf("4) Get server time (requires login)\n");
        printf("5) Quit\n");
        printf("Choice: ");
        fflush(stdout);

        char choice[8];
        if (fgets(choice, sizeof(choice), stdin) == NULL) break;

        switch (choice[0]) {
            case '1': {  /* Register */
                char user[MAX_USERNAME_LEN], pass[MAX_PASSWORD_LEN];
                prompt_line("Choose a username: ", user, sizeof(user));
                prompt_line("Choose a password: ", pass, sizeof(pass));
                snprintf(line, sizeof(line), "%s %s %s", CMD_REGISTER, user, pass);
                if (send_command(sock_fd, line) < 0) { running = 0; }
                break;
            }
            case '2': {  /* Login */
                char user[MAX_USERNAME_LEN], pass[MAX_PASSWORD_LEN];
                prompt_line("Username: ", user, sizeof(user));
                prompt_line("Password: ", pass, sizeof(pass));
                snprintf(line, sizeof(line), "%s %s %s", CMD_LOGIN, user, pass);
                int rc = send_command(sock_fd, line);
                if (rc < 0) { running = 0; }
                else if (rc == 1) {
                    authenticated = 1;
                    strncpy(username, user, sizeof(username) - 1);
                }
                break;
            }
            case '3': {  /* Send data message */
                if (!authenticated) {
                    printf("[!] You must log in first.\n");
                    break;
                }
                char text[LINE_BUF_SIZE - 16];
                prompt_line("Message: ", text, sizeof(text));
                snprintf(line, sizeof(line), "%s %s", CMD_MSG, text);
                if (send_command(sock_fd, line) < 0) running = 0;
                break;
            }
            case '4': {  /* Server time */
                if (!authenticated) {
                    printf("[!] You must log in first.\n");
                    break;
                }
                if (send_command(sock_fd, CMD_TIME) < 0) running = 0;
                break;
            }
            case '5': {  /* Quit */
                send_command(sock_fd, CMD_QUIT);
                running = 0;
                break;
            }
            default:
                printf("[!] Invalid choice.\n");
        }
    }

    close(sock_fd);
    printf("Connection closed.\n");
    return EXIT_SUCCESS;
}
