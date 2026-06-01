/* fuzz_parser.c — libFuzzer harness for the TypeEasy front-end.
 *
 * Target: parse_file() (lexer + Bison parser + AST construction) followed by
 * free_ast() and the per-run global resets. This is the surface where the
 * documented "segfaults intermitentes en paths no cubiertos por los tests"
 * tend to hide: malformed/adversarial source that exercises grammar productions
 * and AST node constructors the curated tests never reach.
 *
 * Why parse-only (no interpret_ast) by default:
 *   Interpreting arbitrary fuzz input would call into the runtime (file IO,
 *   HTTP, DB connections, system()) and easily hang or touch the host. The
 *   parser + AST builder + free_ast path is self-contained, deterministic and
 *   is exactly where memory-safety bugs (UAF / overflow / LLP64 truncation)
 *   surface. Coverage-guided libFuzzer + ASan turns those into hard, reproducible
 *   crashes with a stack trace.
 *
 * Build: see scripts/build_fuzzer.sh (clang -fsanitize=fuzzer,address).
 * Run:   ./fuzz_parser tests/fuzz/corpus -dict=tests/fuzz/typeeasy.dict
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "ast.h"

/* parse_file lives in parser.y; prototype it here so LLP64 (Windows) would
 * never truncate the returned pointer — same rule the rest of the tree follows.
 * The fuzzer only builds on Linux/clang, but keeping the prototype is cheap and
 * documents the contract. */
extern ASTNode *parse_file(FILE *file);

/* Global class registry owned by ast.c. parse_file() registers `class X {...}`
 * declarations into it; resetting between runs keeps each input independent and
 * bounds memory growth across millions of iterations. */
extern int class_count;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* Cap input size: the goal is to find parser/AST bugs, not to DoS the
     * fuzzer with multi-MB inputs that only slow down the corpus. 64 KiB is far
     * larger than any real .te endpoint. */
    if (size > 64 * 1024)
        return 0;

    /* fmemopen gives parse_file() a FILE* over the raw bytes without touching
     * disk. Append a trailing newline (mirrors parse_string() in
     * typeeasy_main.c) so line-oriented lexer rules terminate cleanly, and a
     * NUL so the buffer is always a valid C string for any stray strlen(). */
    char *buf = (char *)malloc(size + 2);
    if (!buf)
        return 0;
    if (size)
        memcpy(buf, data, size);
    buf[size] = '\n';
    buf[size + 1] = '\0';

    FILE *fp = fmemopen(buf, size + 1, "r");
    if (!fp) {
        free(buf);
        return 0;
    }

    ASTNode *root = parse_file(fp);
    fclose(fp);

    if (root)
        free_ast(root);

    /* Per-run teardown: drop classes/vars/flags registered during this parse so
     * the next input starts from a clean interpreter state. detect_leaks is
     * disabled for the fuzzer (the interpreter is short-lived by design and does
     * not free every global at exit); we hunt crashes, not leaks. */
    class_count = 0;
    runtime_reset_vars_to_initial_state();
    te_runtime_reset_flags();

    free(buf);
    return 0;
}
