/* ============================================================================
 * FILE: common.h
 * PART A: SHARED PROTOCOL DEFINITIONS & CONSTANTS
 * ----------------------------------------------------------------------------
 * This header is included by BOTH server.c and client.c so that the two
 * programs always agree on:
 *   - network configuration (port, buffer sizes)
 *   - validation limits (username/password length)
 *   - the text-based application protocol keywords
 *
 * PROTOCOL DESIGN (simple, human-readable, line-based over TCP)
 * ----------------------------------------------------------------------------
 * Every protocol message is a single line of ASCII text terminated by '\n'.
 *
 *   Client -> Server requests:
 *     REGISTER <username> <password>   register a new account
 *     LOGIN    <username> <password>   authenticate an existing account
 *     MSG      <text...>               send data to server (requires login)
 *     TIME                             ask server for its current time
 *     QUIT                             close the connection cleanly
 *
 *   Server -> Client responses:
 *     OK <message>                     request succeeded
 *     ERR <message>                    request failed (reason follows)
 *
 * Keeping the protocol as plain text keeps the assignment easy to test with
 * tools such as `nc` / `telnet` while still demonstrating a real client-side
 * parser and server-side dispatcher.
 * ==========================================================================*/

#ifndef COMMON_H
#define COMMON_H

/* ---- Network configuration ---------------------------------------------- */
#define SERVER_PORT      8080      /* TCP port the server listens on        */
#define LISTEN_BACKLOG   20        /* pending-connection queue for listen() */
#define MAX_CONCURRENT_CLIENTS 50  /* cap on simultaneously served clients  */

/* ---- Buffer / field size limits (used for validation, see server.c) ---- */
#define LINE_BUF_SIZE      1024    /* max size of one protocol line         */
#define MAX_USERNAME_LEN   32
#define MIN_USERNAME_LEN   3
#define MAX_PASSWORD_LEN   64
#define MIN_PASSWORD_LEN   6

/* ---- User database file (Part B of server.c) ---------------------------- */
#define USER_DB_FILE   "users.db"

/* ---- Protocol keywords ---------------------------------------------------*/
#define CMD_REGISTER   "REGISTER"
#define CMD_LOGIN      "LOGIN"
#define CMD_MSG        "MSG"
#define CMD_TIME       "TIME"
#define CMD_QUIT       "QUIT"

#define RESP_OK        "OK"
#define RESP_ERR       "ERR"

#endif /* COMMON_H */
