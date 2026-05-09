#ifndef DEBUGGER_H
#define DEBUGGER_H

#include <time.h>     /* ast.h uses time_t but does not include this itself */
#include "ast.h"

/* Debugger state shared with the interpreter.
 * If g_debug_enabled == 0, all debugger hooks are no-ops (zero overhead path
 * is a single load+test in the dispatch loop). */
extern int g_debug_enabled;
extern const char *g_debug_source_file;   /* user-visible source path (e.g. argv[1]) */

/* Initialise the debugger: open a TCP listener on `port`, accept ONE client
 * (the VS Code debug adapter) and block until the client sends `start`.
 * Sets g_debug_enabled = 1 on success. */
void debugger_init(int port, const char *source_file);

/* Frame management: called by interpret_call_method on entry/exit. */
void debugger_push_frame(const char *name, ASTNode *call_site);
void debugger_pop_frame(void);

/* Statement hook: called from interpret_ast on every node. Cheap when
 * g_debug_enabled == 0 (early return). */
void debugger_on_statement(ASTNode *node);

/* Notify debugger of program termination (sends 'terminated' event). */
void debugger_terminate(int exit_code);

/* Forward stdout/stderr text from the interpreter as an 'output' event so
 * the adapter can show it in the VS Code Debug Console. No-op when
 * g_debug_enabled == 0. */
void debugger_emit_output(const char *category, const char *text);

#endif
