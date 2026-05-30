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

/* Same as above but spawns `num_workers` independent interpreter processes
 * for real CPU parallelism.
 *   - POSIX: fork() + SO_REUSEPORT kernel load balancing.
 *   - Windows: a master process re-spawns N worker processes (each bound to an
 *     internal loopback port) and runs an internal round-robin TCP
 *     load-balancer on the public host:port (Option A, gunicorn-style prefork).
 * num_workers <= 1 runs a single in-process server. `script_path` is the .te
 * file to re-exec for Windows workers (ignored on POSIX). */
int typeeasy_run_api_server_pool(const char *host, int port, int num_workers,
                                 const char *script_path);

/* Internal: run as a single pool worker (used by the Windows load-balancer
 * master when it re-spawns this binary). worker_index >= 0 prints a short
 * per-worker banner instead of the full route listing. */
int typeeasy_run_api_server_worker(const char *host, int port, int worker_index);

/* Configure the value sent in the Access-Control-Allow-Origin header for all
 * responses (and the OPTIONS preflight). Default is "*" (any origin). Pass a
 * concrete origin like "https://app.example.com" to restrict it. */
void typeeasy_set_cors_origin(const char *origin);

#endif
