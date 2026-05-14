#ifndef TYPEEASY_API_SERVER_H
#define TYPEEASY_API_SERVER_H

/* Embedded HTTP server for `typeeasy --api <file.te>` mode.
 *
 * Caller must have already parsed the script and run its global scope, so
 * that `global_methods` contains the route-decorated functions.
 *
 * Blocks until the process is interrupted (SIGINT/SIGTERM).
 * Returns 0 on clean shutdown, non-zero on startup error. */
int typeeasy_run_api_server(const char *host, int port);

#endif
