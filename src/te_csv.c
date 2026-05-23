/* te_csv.c — TypeEasy CSV reader, ORM arena and DataFrame analytics.
 *
 * Extracted from ast.c (Fase 2 paso 2). See te_csv.h for the public API.
 * Everything else in this file (workers, mmap helpers, IO threads, SIMD
 * fast paths, columnar builder, DataFrame ops) stays file-static.
 */

#include "te_csv.h"
#include "ast.h"
#include "te_colcache.h"

#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* ---------- Platform preamble (mirrors ast.c) ------------------------- */
#if defined(__linux__) && defined(__x86_64__)
#  include <sys/mman.h>
#  include <sys/vfs.h>
#  include <pthread.h>
#  include <unistd.h>
#  define TE_HAS_MMAP 1
#  define TE_HAS_PTHREAD 1
#elif defined(_WIN32)
#  include <io.h>
#  include <windows.h>
#  include <pthread.h>
#  define TE_HAS_MMAP 0
#  define TE_HAS_PTHREAD 1
#else
#  ifdef __has_include
#    if __has_include(<unistd.h>)
#      include <unistd.h>
#    endif
#    if __has_include(<pthread.h>)
#      include <pthread.h>
#      define TE_HAS_PTHREAD 1
#    endif
#  endif
#  ifndef TE_HAS_MMAP
#    define TE_HAS_MMAP 0
#  endif
#  ifndef TE_HAS_PTHREAD
#    define TE_HAS_PTHREAD 0
#  endif
#endif

#if defined(__AVX2__)
#  include <immintrin.h>
#  define TE_HAS_AVX2 1
#else
#  define TE_HAS_AVX2 0
#endif

/* Portable cpu count + pread (mingw has no pread). */
#if defined(_WIN32)
static inline long te_nprocs_online(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (long)si.dwNumberOfProcessors;
}
static inline ssize_t te_pread(int fd, void *buf, size_t count, off_t offset) {
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    OVERLAPPED ov; memset(&ov, 0, sizeof(ov));
    ov.Offset     = (DWORD)((unsigned long long)offset & 0xFFFFFFFFu);
    ov.OffsetHigh = (DWORD)(((unsigned long long)offset >> 32) & 0xFFFFFFFFu);
    DWORD got = 0;
    if (!ReadFile(h, buf, (DWORD)count, &got, &ov)) {
        DWORD e = GetLastError();
        if (e == ERROR_HANDLE_EOF) return 0;
        return -1;
    }
    return (ssize_t)got;
}
#  define pread(fd, buf, count, off) te_pread((fd), (buf), (count), (off))
#else
static inline long te_nprocs_online(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? n : 1;
}
#endif

/* ---------- Walker bridges (kept in ast.c) ---------------------------- */
extern void add_or_update_variable(char *id, ASTNode *value);
extern ASTNode *create_ast_leaf_number(char *type, int value, char *str_value, char *id);
extern ObjectNode *create_object(ClassNode *class);
extern void te_colcache_build(ASTNode *list_head, ClassNode *cls);
extern void te_colcache_attach_prebuilt(ASTNode *list_head, struct TeColCache *c);

/* Small FNV-1a duplicate (te_str_hash in ast.c is file-static). */
static uint64_t te_str_hash(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    while (*s) { h ^= (unsigned char)*s++; h *= 1099511628211ULL; }
    return h;
}

/* TEListIdx layout used by walker (must match ast.c). */
typedef struct TEListIdx { int len; int cap; ASTNode **items; } TEListIdx;

/* ====================================================================== *
 * Block extracted verbatim from ast.c (Fase 2 paso 2).                   *
 * ====================================================================== */

/* ---------- CSV reader (RFC 4180 subset) ----------
 * Soporta:
 *   - Campos entre comillas dobles, con "" como escape
 *   - Comas y saltos de línea dentro de comillas
 *   - Terminadores LF, CRLF y CR
 *   - BOM UTF-8 al inicio
 *   - Encabezado mapeado por nombre a los atributos de la clase
 *   - Coerción de tipos según `cls->attributes[i].type` ("int", "string",
 *     "int?", "string?"). Atributos `?` aceptan campos vacíos como `null`.
 * Errores: emiten a stderr y abortan (la llamada se resuelve en parse-time,
 * por lo que `try/catch` aún no está activo).
 *
 * Phase 2: bump-arena para ObjectNode/Variable[]/strings. Reduce ~1.4M
 * mallocs por carga de 200k filas a unas pocas docenas (chunks de 1MB).
 * El arena vive lo que dura el proceso (script mode); nunca se libera
 * individualmente. Esto es seguro porque ningún path en script mode
 * libera obj->attributes[i].* (verificado por inspección).
 */

typedef struct CSVChunk {
    char *base;
    size_t used, cap;
    struct CSVChunk *next;
} CSVChunk;

/* Per-thread head; main thread also uses this (its own __thread slot).
 * After parallel workers finish, their heads are linked into g_csv_arena_keepalive
 * so allocations live until process exit (script-mode, safe to leak). */
static __thread CSVChunk *t_csv_arena = NULL;
static CSVChunk *g_csv_arena_keepalive = NULL;
#if TE_HAS_PTHREAD
static pthread_mutex_t g_csv_keepalive_mu = PTHREAD_MUTEX_INITIALIZER;
#endif

static void csv_arena_keepalive_link(CSVChunk *head) {
    if (!head) return;
    /* Find tail of `head` chain. */
    CSVChunk *tail = head;
    while (tail->next) tail = tail->next;
#if TE_HAS_PTHREAD
    pthread_mutex_lock(&g_csv_keepalive_mu);
#endif
    tail->next = g_csv_arena_keepalive;
    g_csv_arena_keepalive = head;
#if TE_HAS_PTHREAD
    pthread_mutex_unlock(&g_csv_keepalive_mu);
#endif
}

void *csv_arena_alloc(size_t n) {
    n = (n + 7u) & ~(size_t)7u; /* align 8 */
    if (!t_csv_arena || t_csv_arena->used + n > t_csv_arena->cap) {
        size_t cap = n > (8u << 20) ? n : (8u << 20); /* 8 MiB chunks (was 1MB): menos mallocs, mejor localidad por thread */
        CSVChunk *c = (CSVChunk*)malloc(sizeof(CSVChunk));
        c->base = (char*)malloc(cap);
        c->used = 0;
        c->cap = cap;
        c->next = t_csv_arena;
        t_csv_arena = c;
    }
    void *p = t_csv_arena->base + t_csv_arena->used;
    t_csv_arena->used += n;
    return p;
}

char *csv_arena_dup(const char *s, size_t n) {
    char *p = (char*)csv_arena_alloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

char *csv_arena_strdup(const char *s) {
    return csv_arena_dup(s, strlen(s));
}

/* ---------- AST node pool (block allocator) ----------
 * Wrappers OBJECT ASTNode son ~80B y se crean uno por fila. malloc/calloc
 * por wrapper agrega ~5-8 ms en 200k filas. Pool en bloques de 4096 los
 * elimina. type/id se siguen strdup'ando individualmente porque algún
 * path del runtime los puede free()ar (verificado: meterlos a arena
 * crashea con munmap_chunk). El pool en sí mismo se "leakea" al exit.
 */
typedef struct ASTNodePool {
    ASTNode *block;
    int used, cap;
    struct ASTNodePool *next;
} ASTNodePool;

static __thread ASTNodePool *t_ast_pool = NULL;
static ASTNodePool *g_ast_pool_keepalive = NULL;
#if TE_HAS_PTHREAD
static pthread_mutex_t g_ast_pool_keepalive_mu = PTHREAD_MUTEX_INITIALIZER;
#endif

static void ast_pool_keepalive_link(ASTNodePool *head) {
    if (!head) return;
    ASTNodePool *tail = head;
    while (tail->next) tail = tail->next;
#if TE_HAS_PTHREAD
    pthread_mutex_lock(&g_ast_pool_keepalive_mu);
#endif
    tail->next = g_ast_pool_keepalive;
    g_ast_pool_keepalive = head;
#if TE_HAS_PTHREAD
    pthread_mutex_unlock(&g_ast_pool_keepalive_mu);
#endif
}

ASTNode *ast_pool_alloc(void) {
    if (!t_ast_pool || t_ast_pool->used >= t_ast_pool->cap) {
        ASTNodePool *p = (ASTNodePool*)malloc(sizeof(ASTNodePool));
        p->cap = 4096;
        p->block = (ASTNode*)calloc((size_t)p->cap, sizeof(ASTNode));
        p->used = 0;
        p->next = t_ast_pool;
        t_ast_pool = p;
    }
    ASTNode *n = &t_ast_pool->block[t_ast_pool->used++];
    /* Block is calloc'd; slot is already zeroed. */
    return n;
}

/* ---------- Bulk ObjectNode allocator (arena) ----------
 * Para DataFrame.toList() en datasets grandes (12M+ filas), `create_object()`
 * hace ~5 mallocs/objeto: ObjectNode struct, attributes array, strdup(id) y
 * strdup(type) por atributo, y strdup("") por atributo string. A 12M filas
 * eso son ~60M+ mallocs → ~3 min sólo en allocator.
 *
 * Esta variante asigna N objetos en bloques contiguos desde csv_arena_alloc
 * (mismo lifetime: nunca se libera individualmente, vive hasta exit del
 * proceso). Los strings `id` y `type` se comparten con `cls->attributes[a]`
 * (los ClassNode viven hasta exit, no se liberan en el flujo normal).
 *
 * IMPORTANTE: los objetos resultantes NO deben pasarse a `free_object()` ni
 * tener sus `attributes[i].id/type` liberados — pertenecen al arena. El
 * caller (toList) los embebe en una lista de larga vida, así que está bien.
 *
 * Los `value` fields se dejan SIN inicializar (vtype se setea desde el tipo
 * declarado, pero el value lo llena el caller inmediatamente).
 */
ObjectNode *create_objects_bulk(ClassNode *cls, int N) {
    if (!cls || N <= 0) return NULL;
    int nattr = cls->attr_count;
    ObjectNode *objs = (ObjectNode*)csv_arena_alloc((size_t)N * sizeof(ObjectNode));
    Variable *vars = (Variable*)csv_arena_alloc((size_t)N * (size_t)nattr * sizeof(Variable));
    for (int i = 0; i < N; i++) {
        objs[i].class = cls;
        objs[i].attributes = vars + (size_t)i * (size_t)nattr;
        objs[i].owning_list = NULL;
        Variable *va = objs[i].attributes;
        for (int a = 0; a < nattr; a++) {
            /* Comparte punteros con la clase — NO strdup. */
            va[a].id = cls->attributes[a].id;
            va[a].type = cls->attributes[a].type;
            va[a].is_const = 0;
            const char *t = cls->attributes[a].type;
            if (t && strcmp(t, "string") == 0) {
                va[a].vtype = VAL_STRING;
                va[a].value.string_value = NULL; /* caller fills */
            } else if (t && strcmp(t, "float") == 0) {
                va[a].vtype = VAL_FLOAT;
                va[a].value.float_value = 0.0;
            } else {
                va[a].vtype = VAL_INT;
                va[a].value.int_value = 0;
            }
        }
    }
    return objs;
}

/* ---------- pread-based CSV file loader ----------
 * Reemplaza mmap para evitar page-fault overhead en Docker overlay FS / WSL2-9P.
 *
 * PROBLEMA con mmap en Docker bind-mounts (WSL2 9P):
 *   - El archivo está en el host Windows, accedido via Plan-9 (9P protocol).
 *   - mmap paga page faults LAZILY: 3.5MB / 4KB = ~875 page faults, cada una
 *     sincrónica a través de kernel → VFS → overlay → 9P → Windows NTFS.
 *   - Con 12 workers paralelos, los faults se "solapan" pero siguen siendo
 *     costosos (~30-50ms total observado).
 *
 * SOLUCIÓN con pread():
 *   - El kernel recibe UNA petición grande (o pocas) y activa read-ahead
 *     agresivo (FADV_SEQUENTIAL/WILLNEED). El bloque de 3.5MB pasa de host
 *     a page-cache Linux en una sola ráfaga.
 *   - Con N threads haciendo pread() en paralelo sobre chunks, el SO puede
 *     pipelinear múltiples peticiones 9P.
 *   - El buffer resultante es malloc'd y WRITABLE (igual que mmap MAP_PRIVATE).
 *     Los parsers pueden escribir \0 in-place sin problema.
 *
 * NOTA: si el archivo ya está en page-cache (segunda ejecución), pread es
 *       casi gratis (~1-2ms). mmap también, pero sigue pagando TLB misses.
 */
#if TE_HAS_PTHREAD
typedef struct { int fd; char *buf; off_t offset; size_t size; } CSVPreadArgs;
static void *csv_pread_io_worker(void *p) {
    CSVPreadArgs *a = (CSVPreadArgs*)p;
    size_t done = 0;
    while (done < a->size) {
        ssize_t r = pread(a->fd, a->buf + (size_t)a->offset + done,
                          a->size - done, a->offset + (off_t)done);
        if (r <= 0) break;
        done += (size_t)r;
    }
    return NULL;
}
#endif

/* Forward declare: definida más abajo (mmap-based loader). */
static char *csv_mmap_file(const char *filename, size_t *out_len);
static char *csv_read_all(FILE *fp, size_t *out_len);

static char *csv_read_file(const char *filename, size_t *out_len, int *out_is_mmap) {
    if (out_is_mmap) *out_is_mmap = 0;
    /* Override por env: TE_CSV_IO=mmap|pread fuerza el path. Útil para A/B
     * benchmarks en Docker Desktop (WSL2 overlay puede ser más lento via
     * mmap que via parallel pread). Default: auto-detect. */
    const char *io_force = getenv("TE_CSV_IO");
    int force_pread = (io_force && (!strcmp(io_force, "pread") || !strcmp(io_force, "PREAD")));
    int force_mmap  = (io_force && (!strcmp(io_force, "mmap")  || !strcmp(io_force, "MMAP")));
    /* Auto-detect: usar mmap en ext4/overlayfs (rápido, zero-copy),
     * pread paralelo en V9FS/FUSE (evita page-faults a través de 9P).
     * V9FS_MAGIC = 0x01021997 (WSL2 bind-mount desde Windows).      */
#if defined(__linux__)
    struct statfs sfs;
    int on_v9fs = 0;
    if (statfs(filename, &sfs) == 0 && (long)sfs.f_type == 0x01021997L)
        on_v9fs = 1;
    if ((!on_v9fs && !force_pread) || force_mmap) {
        /* ext4 / overlayfs: mmap MAP_PRIVATE (zero-copy, demand paging). */
        char *m = csv_mmap_file(filename, out_len);
        if (m && out_is_mmap) *out_is_mmap = 1;
        return m;
    }
#else
    if (force_mmap) {
        char *m = csv_mmap_file(filename, out_len);
        if (m && out_is_mmap) *out_is_mmap = 1;
        return m;
    }
    (void)force_pread;
#endif
    /* V9FS o fallback: pread paralelo (4 threads, evita page-faults VFS). */
    int fd = open(filename, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return NULL; }
    if (st.st_size == 0) {
        close(fd); *out_len = 0;
        char *p = (char*)malloc(1); if (p) p[0] = '\0'; return p;
    }
    size_t size = (size_t)st.st_size;
    /* +1 para null-terminator de seguridad al final del buffer. */
    char *buf = (char*)malloc(size + 1);
    if (!buf) { close(fd); return NULL; }
    buf[size] = '\0';

    /* Hints al kernel: activa read-ahead agresivo antes de los pread(). */
#ifdef POSIX_FADV_SEQUENTIAL
    posix_fadvise(fd, 0, (off_t)size, POSIX_FADV_SEQUENTIAL);
#endif
#ifdef POSIX_FADV_WILLNEED
    posix_fadvise(fd, 0, (off_t)size, POSIX_FADV_WILLNEED);
#endif

#if TE_HAS_PTHREAD
    /* Múltiples pread paralelos saturan el queue 9P de WSL2 → mayor throughput.
     * 4 threads = sweet spot para bind-mount Windows→WSL2→Docker. */
    long ncpu = te_nprocs_online();
    int nio = (int)ncpu;
    if (nio > 4) nio = 4;
    if (nio < 1) nio = 1;
    if (size >= 256u * 1024u && nio >= 2) {
        size_t chunk = size / (size_t)nio;
        CSVPreadArgs *args = (CSVPreadArgs*)malloc((size_t)nio * sizeof(CSVPreadArgs));
        pthread_t *tids   = (pthread_t*)malloc((size_t)nio * sizeof(pthread_t));
        for (int i = 0; i < nio; i++) {
            args[i].fd     = fd;
            args[i].buf    = buf;
            args[i].offset = (off_t)((size_t)i * chunk);
            args[i].size   = (i == nio - 1) ? (size - (size_t)i * chunk) : chunk;
        }
        for (int i = 0; i < nio; i++)
            pthread_create(&tids[i], NULL, csv_pread_io_worker, &args[i]);
        for (int i = 0; i < nio; i++)
            pthread_join(tids[i], NULL);
        free(tids); free(args);
    } else
#endif
    {
        /* Serial read (loop para manejar reads parciales). */
        size_t nread = 0;
        while (nread < size) {
            ssize_t r = read(fd, buf + nread, size - nread);
            if (r <= 0) break;
            nread += (size_t)r;
        }
    }
    close(fd);
    *out_len = size;
    return buf;
}

/* ---------- mmap-based CSV loader (mantenido como referencia) ----------
 * MAP_PRIVATE permite escribir (\0 in-place) sin tocar el archivo.
 * REEMPLAZADO por csv_read_file en from_csv_to_list — ver comentario arriba. */
static char *csv_mmap_file(const char *filename, size_t *out_len) {
#if TE_HAS_MMAP
    int fd = open(filename, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return NULL; }
    if (st.st_size == 0) {
        close(fd);
        *out_len = 0;
        char *p = (char*)malloc(1);
        if (p) p[0] = '\0';
        return p;
    }
    void *m = mmap(NULL, (size_t)st.st_size,
                   PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    close(fd); /* fd ya no necesario tras mmap */
    if (m == MAP_FAILED) return NULL;
    /* Hint: lectura secuencial. Mejor prefetch del kernel. */
    madvise(m, (size_t)st.st_size, MADV_SEQUENTIAL);
    /* MADV_WILLNEED async-prefetcha; en Docker overlay FS suele dar speedup
     * marginal. MADV_POPULATE_READ (Linux 5.14+) bloquea sincr\u00f3nicamente y
     * tend\u00eda a EMPEORAR el total en estos benches \u2014 omitido. */
#ifdef MADV_WILLNEED
    madvise(m, (size_t)st.st_size, MADV_WILLNEED);
#endif
    *out_len = (size_t)st.st_size;
    return (char*)m;
#else
    FILE *fp = fopen(filename, "rb");
    if (!fp) return NULL;
    char *buf = csv_read_all(fp, out_len);
    fclose(fp);
    return buf;
#endif
}

/* ---------- SIMD scanner ----------
 * Encuentra el primer ',' '\n' '\r' a partir de `i`. Para AVX2 procesa
 * 32 bytes por iteración. Solo se usa en estado NO-quoted del scanner
 * (mayoría abrumadora de campos en CSVs reales). */
static inline size_t simd_find_csv_delim(const char *s, size_t i, size_t len) {
#if TE_HAS_AVX2
    const __m256i v_comma = _mm256_set1_epi8(',');
    const __m256i v_lf    = _mm256_set1_epi8('\n');
    const __m256i v_cr    = _mm256_set1_epi8('\r');
    while (i + 32 <= len) {
        __m256i v = _mm256_loadu_si256((const __m256i*)(s + i));
        __m256i m = _mm256_or_si256(
            _mm256_or_si256(_mm256_cmpeq_epi8(v, v_comma),
                            _mm256_cmpeq_epi8(v, v_lf)),
            _mm256_cmpeq_epi8(v, v_cr));
        unsigned mask = (unsigned)_mm256_movemask_epi8(m);
        if (mask) return i + (size_t)__builtin_ctz(mask);
        i += 32;
    }
#endif
    while (i < len && s[i] != ',' && s[i] != '\n' && s[i] != '\r') i++;
    return i;
}

static char *csv_read_all(FILE *fp, size_t *out_len) {
    size_t cap = 8192, len = 0;
    char *buf = (char*)malloc(cap);
    if (!buf) return NULL;
    size_t n;
    while ((n = fread(buf + len, 1, cap - len, fp)) > 0) {
        len += n;
        if (len == cap) {
            size_t ncap = cap * 2;
            char *nb = (char*)realloc(buf, ncap);
            if (!nb) { free(buf); return NULL; }
            buf = nb; cap = ncap;
        }
    }
    *out_len = len;
    return buf;
}

static char *csv_next_field(const char *src, size_t len, size_t *pos, int *end_of_record) {
    size_t i = *pos;
    size_t fcap = 64, flen = 0;
    char *f = (char*)malloc(fcap);
    int in_quotes = 0;
    *end_of_record = 0;

    if (i < len && src[i] == '"') { in_quotes = 1; i++; }

    while (i < len) {
        char c = src[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < len && src[i+1] == '"') {
                    if (flen + 1 >= fcap) { fcap *= 2; f = (char*)realloc(f, fcap); }
                    f[flen++] = '"';
                    i += 2;
                } else {
                    in_quotes = 0; i++;
                }
            } else {
                if (flen + 1 >= fcap) { fcap *= 2; f = (char*)realloc(f, fcap); }
                f[flen++] = c;
                i++;
            }
        } else {
            if (c == ',') { i++; break; }
            if (c == '\n' || c == '\r') {
                if (c == '\r' && i + 1 < len && src[i+1] == '\n') i++;
                i++;
                *end_of_record = 1;
                break;
            }
            if (flen + 1 >= fcap) { fcap *= 2; f = (char*)realloc(f, fcap); }
            f[flen++] = c;
            i++;
        }
    }
    if (i >= len) *end_of_record = 1;
    f[flen] = '\0';
    *pos = i;
    return f;
}

static char **csv_next_record(const char *src, size_t len, size_t *pos, int *out_count) {
    while (*pos < len && (src[*pos] == '\n' || src[*pos] == '\r')) (*pos)++;
    if (*pos >= len) { *out_count = 0; return NULL; }

    size_t cap = 8, n = 0;
    char **fields = (char**)malloc(cap * sizeof(char*));
    int eor = 0;
    while (!eor) {
        if (n == cap) { cap *= 2; fields = (char**)realloc(fields, cap * sizeof(char*)); }
        fields[n++] = csv_next_field(src, len, pos, &eor);
    }
    *out_count = (int)n;
    return fields;
}

static void csv_free_record(char **rec, int n) {
    for (int i = 0; i < n; i++) free(rec[i]);
    free(rec);
}

/* Versión que reusa un fields-array fijo (pre-dimensionado a max_cols).
 * Si una fila excede max_cols, descarta los campos extra. */
static int csv_next_record_into(const char *src, size_t len, size_t *pos,
                                 char **fields, int max_cols) {
    while (*pos < len && (src[*pos] == '\n' || src[*pos] == '\r')) (*pos)++;
    if (*pos >= len) return 0;
    int n = 0, eor = 0;
    while (!eor) {
        if (n >= max_cols) {
            char *junk = csv_next_field(src, len, pos, &eor);
            free(junk);
            continue;
        }
        fields[n++] = csv_next_field(src, len, pos, &eor);
    }
    return n;
}

static void csv_free_fields_inplace(char **fields, int n) {
    for (int i = 0; i < n; i++) free(fields[i]);
}

/* ZERO-COPY scanner: muta `src` (escribe '\0' en separadores) y devuelve
 * punteros directos al buffer. Para campos no quoted (caso común), no
 * hay malloc/copy. Para campos quoted con `""` necesitamos in-place
 * collapse de las dobles-quotes (también sin alloc — escribimos en el
 * mismo span del field). El caller NO debe free() los punteros.
 *
 * Returns: número de campos llenados en `fields` (truncado a max_cols).
 *          -1 si no hay más datos. Mutua `*pos`. */
static int csv_next_record_zerocopy(char *src, size_t len, size_t *pos,
                                     char **fields, int max_cols) {
    /* Consume linebreaks separadores entre records. */
    while (*pos < len && (src[*pos] == '\n' || src[*pos] == '\r')) (*pos)++;
    if (*pos >= len) return -1;

    int n = 0;
    while (1) {
        size_t i = *pos;
        char *field_start;
        int has_escape = 0;

        if (i < len && src[i] == '"') {
            /* Quoted field. */
            i++;
            field_start = src + i;
            char *write = src + i;
            while (i < len) {
                char c = src[i];
                if (c == '"') {
                    if (i + 1 < len && src[i+1] == '"') {
                        /* "" → " (collapse in place). */
                        *write++ = '"';
                        i += 2;
                        has_escape = 1;
                    } else {
                        i++;
                        break; /* end of quoted field */
                    }
                } else {
                    if (has_escape) *write++ = c;
                    i++;
                }
            }
            if (!has_escape) {
                /* No escape → terminar al cierre de la quote. */
                /* `i` apunta justo después del closing quote. Marcar fin. */
                src[i - 1] = '\0';
            } else {
                *write = '\0';
            }
            /* Saltar al siguiente delimitador. */
            i = simd_find_csv_delim(src, i, len);
        } else {
            field_start = src + i;
            i = simd_find_csv_delim(src, i, len);
            /* `i` apunta al delimitador. Lo NUL-terminamos para hacer el
             * span un C-string (mutación destructiva del source). */
            if (i < len) {
                /* Lo guardamos antes de pisar para detectar fin de record. */
                /* (lo manejamos abajo). */
            }
        }

        /* Determinar fin de record y avanzar pos. */
        int eor = 0;
        if (i >= len) {
            eor = 1;
            *pos = i;
        } else {
            char d = src[i];
            src[i] = '\0';      /* terminate field */
            if (d == ',') {
                *pos = i + 1;
            } else {
                /* '\n' o '\r' */
                size_t k = i + 1;
                if (d == '\r' && k < len && src[k] == '\n') k++;
                *pos = k;
                eor = 1;
            }
        }

        if (n < max_cols) {
            fields[n++] = field_start;
        }
        /* Si overflow (más campos que max_cols), descartamos sin guardar. */

        if (eor) break;
    }
    return n;
}

/* ------ Scanner readonly (no escribe \0): para buffers mmap MAP_PRIVATE. ------
 * Retorna pares (ptr, len) en lugar de punteros null-terminados.
 * Evita los COW page faults que disparan los writes en overlayfs (~100μs/page). */
static int csv_next_record_readonly(const char *src, size_t len, size_t *pos,
                                    const char **fields, int *field_lens, int max_cols) {
    while (*pos < len && (src[*pos] == '\n' || src[*pos] == '\r')) (*pos)++;
    if (*pos >= len) return -1;

    int n = 0;
    while (1) {
        size_t i = *pos;
        const char *field_start;
        int field_len;

        if (i < len && src[i] == '"') {
            /* Quoted: field_start after opening quote, len until closing quote. */
            i++;
            field_start = src + i;
            while (i < len && !(src[i] == '"' && (i+1 >= len || src[i+1] != '"'))) {
                if (src[i] == '"' && i+1 < len && src[i+1] == '"') i += 2;
                else i++;
            }
            field_len = (int)(i - (size_t)(field_start - src));
            if (i < len) i++; /* skip closing quote */
            i = simd_find_csv_delim(src, i, len);
        } else {
            field_start = src + i;
            i = simd_find_csv_delim(src, i, len);
            field_len = (int)(i - (size_t)(field_start - src));
        }

        int eor = 0;
        if (i >= len) {
            eor = 1;
            *pos = i;
        } else {
            char d = src[i];
            if (d == ',') {
                *pos = i + 1;
            } else {
                size_t k = i + 1;
                if (d == '\r' && k < len && src[k] == '\n') k++;
                *pos = k;
                eor = 1;
            }
        }

        if (n < max_cols) {
            fields[n]     = field_start;
            field_lens[n] = field_len;
            n++;
        }
        if (eor) break;
    }
    return n;
}

int csv_attr_is_int(const char *t) {
    return t && (!strcmp(t, "int") || !strcmp(t, "int?"));
}
int csv_attr_is_string(const char *t) {
    return t && (!strcmp(t, "string") || !strcmp(t, "string?"));
}
int csv_attr_is_nullable(const char *t) {
    if (!t) return 0;
    size_t L = strlen(t);
    return L > 0 && t[L-1] == '?';
}

/* Sentinel global: cuando un wrapper CSV ASTNode tiene `type` apuntando
 * a este string compartido, free_ast() lo deja vivo (evita strdup por fila). */
char *g_csv_wrapper_obj_type = NULL;

/* ---------- Fast-row primitives (export público para MySQL ORM, etc.) ----------
 * Wrappers no-static sobre las primitivas internas. Mantenemos las internas
 * estáticas para no exponer estructuras (CSVChunk, ASTNodePool). */
void *te_orm_arena_alloc(size_t n) { return csv_arena_alloc(n); }
char *te_orm_arena_strdup(const char *s) { return csv_arena_strdup(s); }
char *te_orm_arena_dup(const char *s, size_t n) { return csv_arena_dup(s, n); }
ASTNode *te_orm_pool_alloc(void) { return ast_pool_alloc(); }
const char *te_orm_wrapper_obj_type(void) {
    if (!g_csv_wrapper_obj_type) g_csv_wrapper_obj_type = csv_arena_strdup("OBJECT");
    return g_csv_wrapper_obj_type;
}
int te_orm_attr_kind(const char *t) {
    if (csv_attr_is_int(t)) return 0;
    if (csv_attr_is_string(t)) return 1;
    return 2;
}
int te_orm_attr_is_nullable(const char *t) { return csv_attr_is_nullable(t); }

/* ---------- Parallel CSV parsing ----------
 * Cada worker procesa un rango [start, end) del buffer (mmap MAP_PRIVATE).
 * Limitación: asume que '\n' = fin de fila (no quoted-newlines). El
 * caller verifica esto antes de paralelizar.
 *
 * Boundary handling:
 *   - Worker 0 procesa desde data_start.
 *   - Worker i>0 avanza pos hasta el primer '\n'+1 dentro del chunk
 *     (descarta la fila parcial, que será procesada por worker i-1).
 *   - Worker procesa filas que COMIENZAN antes de end. La última puede
 *     cruzar end (lectura segura: nunca pasa de total_len).
 */

/* ============================================================
 * Ruta COLUMNAR (DataFrame-style): activada por env TE_CSV_DATAFRAME=1.
 * En lugar de crear un ObjectNode + Variable[] + ASTNode-wrapper por fila,
 * llena buffers de columnas contiguos (int*, char**) tipo Apache Arrow.
 *
 * Limitaciones (intencionalmente estrictas, fallback al path normal si no se
 * cumplen):
 *   - Solo atributos `int` y `string` (no `int?` / `string?` / otros).
 *   - Sin quoted fields ('"' en archivo → fallback).
 *
 * Resultado: wrapper ASTNode tipo "LIST" con left=NULL pero TEListIdx pre-set
 * con len=row_count. Por tanto `df.length` retorna O(1) sin objetos. Iteración
 * o indexación NO está soportada en este wrapper (TODO: materialización lazy).
 * Diseñado para análisis tipo "load + count/aggregate" que es el caso polars.
 * ============================================================*/
typedef struct DataFrame {
    int row_count;
    int col_count;       /* = nattr */
    int *col_kinds;      /* 0=int, 1=string */
    void **col_data;     /* col_data[a] points to int64_t* (kind=0) or char** (kind=1), length row_count */
    char **col_names;    /* aliases to interned/arena names */
    ClassNode *cls;
} DataFrame;

/* SIMD count of '\n' bytes in [s+lo, s+hi). */
static size_t csv_count_newlines(const char *s, size_t lo, size_t hi) {
    size_t i = lo, n = 0;
#if TE_HAS_AVX2
    __m256i nl = _mm256_set1_epi8('\n');
    while (i + 32 <= hi) {
        __m256i v = _mm256_loadu_si256((const __m256i*)(s + i));
        __m256i eq = _mm256_cmpeq_epi8(v, nl);
        unsigned m = (unsigned)_mm256_movemask_epi8(eq);
        n += (size_t)__builtin_popcount(m);
        i += 32;
    }
#endif
    while (i < hi) { if (s[i] == '\n') n++; i++; }
    return n;
}

/* ============================================================
 * CSVColWorkerArgs — Phase A + spinwait + Phase B (sin barriers/futex).
 *
 * Flujo prefix-sum con spinwait (elimina ~10ms de futex overhead):
 *   Phase A (paralelo): worker cuenta \n, pone phase_a_done=1 (release).
 *   Main spinea sobre phase_a_done[] (sin syscall) → prefix-sum → alloc.
 *   Main pone go_phase_b=1 (release) para cada worker.
 *   Phase B (paralelo): worker spinea go_phase_b, luego parsea directo al
 *                        global array. Sin locks, sin memcpy.
 * ============================================================ */
typedef struct CSVColWorkerArgs {
    char       *src;
    size_t      total_len;
    size_t      chunk_start;
    size_t      chunk_end;
    int         is_first;
    int         nattr;
    int         header_n;
    const int  *attr_kind;
    const int  *col_to_attr;
    size_t      actual_parse_start;
    int         row_count;
    int         row_offset;
    void      **global_col_data;
    /* Spinwait flags (cache-line aligned para evitar false sharing) */
    volatile int phase_a_done __attribute__((aligned(64)));
    volatile int go_phase_b   __attribute__((aligned(64)));
    /* Flag: si 1, el buffer src es mmap'd (read-only seguro) →
     * usar csv_next_record_readonly (sin writes, sin COW page faults). */
    int         readonly_src;
    /* Pool: fase actual (0=idle, 1=phase_a, 2=phase_b, 3=exit).
     * Cache-line aligned para evitar false sharing entre slots. */
    volatile int pool_phase __attribute__((aligned(64)));
} CSVColWorkerArgs;

#if TE_HAS_PTHREAD
static inline void cpu_relax(void) {
#if defined(__x86_64__)
    __builtin_ia32_pause();
#else
    __asm__ volatile("" ::: "memory");
#endif
}

/* === csv_do_phase_b ===
 * Parsea el chunk asignado y escribe directamente en los arrays globales del DataFrame.
 * Precondición: a->actual_parse_start, a->row_offset, a->global_col_data están listos. */
static void csv_do_phase_b(CSVColWorkerArgs *a) {
    char       *src       = a->src;
    size_t      total_len = a->total_len;
    size_t      pos       = a->actual_parse_start;
    size_t      end       = a->chunk_end;
    int         header_n  = a->header_n;
    const int  *attr_kind = a->attr_kind;
    const int  *col_to_attr = a->col_to_attr;
    void      **gcols     = a->global_col_data;
    int         row_off   = a->row_offset;

    char **rec_fields = (char**)malloc(header_n * sizeof(char*));
    int row_idx = 0;

    if (a->readonly_src) {
        /* === Readonly path: sin writes → sin COW en overlayfs/mmap. === */
        const char **ro_fields = (const char**)rec_fields; /* reuse buffer */
        int *field_lens = (int*)malloc(header_n * sizeof(int));
        while (pos < end) {
            size_t row_begin = pos;
            int rn = csv_next_record_readonly(src, total_len, &pos,
                                              ro_fields, field_lens, header_n);
            if (rn <= 0) break;
            if (row_begin >= end) break;
            if (rn == 1 && field_lens[0] == 0) continue;

            int global_row = row_off + row_idx;
            int upto = rn < header_n ? rn : header_n;
            for (int c = 0; c < upto; c++) {
                int aa = col_to_attr[c];
                if (aa < 0) continue;
                const char *raw = ro_fields[c];
                int rawlen     = field_lens[c];
                if (attr_kind[aa] == 0 /*INT*/) {
                    const char *q = raw;
                    const char *q_end = raw + rawlen;
                    int neg = 0;
                    if (q < q_end && *q == '-') { neg = 1; q++; }
                    else if (q < q_end && *q == '+') { q++; }
                    long v = 0;
                    while (q < q_end) {
                        unsigned d = (unsigned)(*q - '0');
                        if (d > 9u) { v = 0; break; }
                        v = v * 10 + (long)d; q++;
                    }
                    if (neg) v = -v;
                    ((int64_t*)gcols[aa])[global_row] = (int64_t)v;
                } else {
                    /* String: guardamos el puntero sin null-terminar.
                     * Válido en el buffer mmap'd (lifetime de proceso). */
                    ((const char**)gcols[aa])[global_row] = raw;
                }
            }
            row_idx++;
        }
        free(field_lens);
    } else {
        /* === Writable path: escribe \0 (buffer pread malloc'd). === */
        while (pos < end) {
            size_t row_begin = pos;
            int rn = csv_next_record_zerocopy(src, total_len, &pos, rec_fields, header_n);
            if (rn <= 0) break;
            if (row_begin >= end) break;
            if (rn == 1 && rec_fields[0][0] == '\0') continue;

            int global_row = row_off + row_idx;
            int upto = rn < header_n ? rn : header_n;
            for (int c = 0; c < upto; c++) {
                int aa = col_to_attr[c];
                if (aa < 0) continue;
                const char *raw = rec_fields[c];
                if (attr_kind[aa] == 0 /*INT*/) {
                    const char *q = raw;
                    int neg = 0;
                    if (*q == '-') { neg = 1; q++; }
                    else if (*q == '+') { q++; }
                    long v = 0;
                    int ok = (*q != '\0');
                    while (*q) {
                        unsigned d = (unsigned)(*q - '0');
                        if (d > 9u) { ok = 0; break; }
                        v = v * 10 + (long)d; q++;
                    }
                    if (!ok) {
                        char *endp = NULL;
                        v = strtol(raw, &endp, 10);
                    } else if (neg) v = -v;
                    ((int64_t*)gcols[aa])[global_row] = (int64_t)v;
                } else {
                    ((char**)gcols[aa])[global_row] = (char*)raw;
                }
            }
            row_idx++;
        }
    }
    free(rec_fields);
    a->row_count = row_idx;
}

static void *csv_combined_col_worker(void *p) {
    CSVColWorkerArgs *a = (CSVColWorkerArgs*)p;

    /* === Phase A: boundary adjustment + SIMD row count === */
    {
        char   *src       = a->src;
        size_t  total_len = a->total_len;
        size_t  pos       = a->chunk_start;
        if (!a->is_first && pos > 0 && src[pos - 1] != '\n') {
            while (pos < total_len && src[pos] != '\n') pos++;
            if (pos < total_len) pos++;
        }
        a->actual_parse_start = pos;
        a->row_count = (int)csv_count_newlines(src, pos, a->chunk_end);
    }

    /* Señalar Phase A completa (release → main la ve sin lag). */
    __atomic_store_n(&a->phase_a_done, 1, __ATOMIC_RELEASE);

    /* Spinear hasta que main señale Phase B (acquire → ve global_col_data). */
    while (!__atomic_load_n(&a->go_phase_b, __ATOMIC_ACQUIRE))
        cpu_relax();

    /* Phase B: skip si main no pudo alocar. */
    if (!a->global_col_data) return NULL;

    /* === Phase B: parsear y escribir directo al DataFrame global === */
    csv_do_phase_b(a);
    return NULL;
}

/* === Global CSV Worker Pool ===
 * Threads inicializados al startup del programa (antes del primer CSV load).
 * Esto elimina el overhead de pthread_create por cada carga de CSV en Docker
 * (~3ms/thread × 11 threads = ~33ms por carga → 0ms con pool pre-iniciado).
 * Workers esperan tareas con spinwait + sched_yield (idle ≈ 0% CPU). */
#define CSV_POOL_IDLE    0
#define CSV_POOL_PHASE_A 1
#define CSV_POOL_PHASE_B 2
#define CSV_POOL_EXIT    3
#define CSV_POOL_MAX     15  /* máximo de worker threads en pool (main es el +1) */

static CSVColWorkerArgs g_csv_slots[CSV_POOL_MAX];
static pthread_t        g_csv_pthreads[CSV_POOL_MAX];
static volatile int     g_csv_pool_n = 0;  /* threads vivos en pool */
static volatile int     g_csv_pool_ready_count = 0; /* threads que llegaron al idle loop */

static void *csv_pool_worker_fn(void *arg) {
    CSVColWorkerArgs *s = (CSVColWorkerArgs*)arg;
    /* Notificar al main que este thread llegó al idle spinwait loop. */
    __atomic_fetch_add(&g_csv_pool_ready_count, 1, __ATOMIC_RELEASE);
    int idle_spin = 0;
    while (1) {
        int p = __atomic_load_n(&s->pool_phase, __ATOMIC_ACQUIRE);
        if (p == CSV_POOL_IDLE) {
            cpu_relax();
            /* Evitar quemar CPU cuando no hay trabajo: yield periódicamente. */
            if (++idle_spin > 5000) { sched_yield(); idle_spin = 0; }
            continue;
        }
        idle_spin = 0;
        if (p == CSV_POOL_EXIT) return NULL;
        if (p == CSV_POOL_PHASE_A) {
            /* Phase A: boundary adjustment + SIMD count */
            size_t pos2 = s->chunk_start;
            if (!s->is_first && pos2 > 0 && s->src[pos2 - 1] != '\n') {
                while (pos2 < s->total_len && s->src[pos2] != '\n') pos2++;
                if (pos2 < s->total_len) pos2++;
            }
            s->actual_parse_start = pos2;
            s->row_count = (int)csv_count_newlines(s->src, pos2, s->chunk_end);
        } else if (p == CSV_POOL_PHASE_B) {
            /* Phase B: parsear y escribir en arrays globales */
            csv_do_phase_b(s);
        }
        /* Señalar idle (done) → main lo detecta sin spinwait externo */
        __atomic_store_n(&s->pool_phase, CSV_POOL_IDLE, __ATOMIC_RELEASE);
    }
}

/* Inicializar pool global. Llamar desde main() antes de ejecutar el script.
 * n = número total de workers (incluyendo main); crea n-1 pool threads.
 * BLOQUEA hasta que todos los threads estén listos en el idle spinwait loop.
 * Esto mueve el overhead de pthread_create (~3ms/thread en Docker) al startup,
 * ANTES del timer de CSV, para que los loads posteriores sean instantáneos. */
void te_csv_pool_init(int n) {
    /* DESHABILITADO: el pool path causaba starvation del main por workers
     * spinning en cores. Mantener el stub para compat con typeeasy_main.c. */
    (void)n;
}
#else /* !TE_HAS_PTHREAD */
void te_csv_pool_init(int n) { (void)n; }
#endif /* TE_HAS_PTHREAD */

/* ============================================================
 * DataFrame analytics (Fases 1-3, v0.0.11-pre):
 *   - Fase 1: group_sum columnar nativo (hash table open-addressing)
 *   - Fase 2: SIMD AVX2 sum/min/max sobre columnas int
 *   - Fase 3: pthread parallel reductions (worker pool ad-hoc)
 *
 * Wrapper LIST DataFrame-backed: TEListIdx con cap = TE_DF_SENTINEL
 * y items = (ASTNode**)(void*)DataFrame*. Helper te_list_df() extrae.
 * ============================================================ */
#define TE_DF_SENTINEL (-0x0DF0DF)

DataFrame *te_list_df(ASTNode *list) {
    if (!list || !list->type || strcmp(list->type, "LIST") != 0) return NULL;
    TEListIdx *ix = (TEListIdx*)list->extra;
    if (!ix || ix->cap != TE_DF_SENTINEL) return NULL;
    return (DataFrame*)(void*)ix->items;
}

static ASTNode *te_df_wrap(DataFrame *df) {
    if (!df) return NULL;
    ASTNode *out = (ASTNode*)calloc(1, sizeof(ASTNode));
    out->type = strdup("LIST");
    out->left = NULL;
    out->value = 1;
    TEListIdx *ix = (TEListIdx*)calloc(1, sizeof(TEListIdx));
    ix->len = df->row_count;
    ix->cap = TE_DF_SENTINEL;
    ix->items = (ASTNode**)(void*)df;
    out->extra = (struct ASTNode*)ix;
    return out;
}

static int df_col_index(const DataFrame *df, const char *name) {
    if (!df || !name) return -1;
    for (int i = 0; i < df->col_count; i++)
        if (df->col_names[i] && strcmp(df->col_names[i], name) == 0) return i;
    return -1;
}

/* ---- Fase 2/4: SIMD AVX2 reductions sobre int64 columns ---- */
static long long df_sum_i64_scalar(const int64_t *a, size_t n) {
    long long s = 0;
    for (size_t i = 0; i < n; i++) s += (long long)a[i];
    return s;
}
static long long df_min_i64_scalar(const int64_t *a, size_t n) {
    long long m = (long long)a[0];
    for (size_t i = 1; i < n; i++) if ((long long)a[i] < m) m = (long long)a[i];
    return m;
}
static long long df_max_i64_scalar(const int64_t *a, size_t n) {
    long long m = (long long)a[0];
    for (size_t i = 1; i < n; i++) if ((long long)a[i] > m) m = (long long)a[i];
    return m;
}

#if defined(__AVX2__)
/* 4 lanes int64 por vector. Sum: _mm256_add_epi64 directo (sin extender). */
static long long df_sum_i64_avx2(const int64_t *a, size_t n) {
    __m256i acc0 = _mm256_setzero_si256();
    __m256i acc1 = _mm256_setzero_si256();
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        __m256i v0 = _mm256_loadu_si256((const __m256i*)(a + i));
        __m256i v1 = _mm256_loadu_si256((const __m256i*)(a + i + 4));
        acc0 = _mm256_add_epi64(acc0, v0);
        acc1 = _mm256_add_epi64(acc1, v1);
    }
    __m256i acc = _mm256_add_epi64(acc0, acc1);
    long long buf[4];
    _mm256_storeu_si256((__m256i*)buf, acc);
    long long s = buf[0] + buf[1] + buf[2] + buf[3];
    for (; i < n; i++) s += (long long)a[i];
    return s;
}
/* AVX2 no tiene _mm256_min/max_epi64. Emulamos con cmpgt+blendv. */
static inline __m256i te_mm256_min_epi64(__m256i a, __m256i b) {
    __m256i cmp = _mm256_cmpgt_epi64(a, b);          /* a > b -> all 1s */
    return _mm256_blendv_epi8(a, b, cmp);            /* pick b if a>b */
}
static inline __m256i te_mm256_max_epi64(__m256i a, __m256i b) {
    __m256i cmp = _mm256_cmpgt_epi64(a, b);
    return _mm256_blendv_epi8(b, a, cmp);            /* pick a if a>b */
}
static long long df_min_i64_avx2(const int64_t *a, size_t n) {
    if (n == 0) return 0;
    if (n < 4) return df_min_i64_scalar(a, n);
    __m256i acc = _mm256_loadu_si256((const __m256i*)a);
    size_t i = 4;
    for (; i + 4 <= n; i += 4) {
        __m256i v = _mm256_loadu_si256((const __m256i*)(a + i));
        acc = te_mm256_min_epi64(acc, v);
    }
    long long buf[4];
    _mm256_storeu_si256((__m256i*)buf, acc);
    long long m = buf[0];
    for (int k = 1; k < 4; k++) if (buf[k] < m) m = buf[k];
    for (; i < n; i++) if ((long long)a[i] < m) m = (long long)a[i];
    return m;
}
static long long df_max_i64_avx2(const int64_t *a, size_t n) {
    if (n == 0) return 0;
    if (n < 4) return df_max_i64_scalar(a, n);
    __m256i acc = _mm256_loadu_si256((const __m256i*)a);
    size_t i = 4;
    for (; i + 4 <= n; i += 4) {
        __m256i v = _mm256_loadu_si256((const __m256i*)(a + i));
        acc = te_mm256_max_epi64(acc, v);
    }
    long long buf[4];
    _mm256_storeu_si256((__m256i*)buf, acc);
    long long m = buf[0];
    for (int k = 1; k < 4; k++) if (buf[k] > m) m = buf[k];
    for (; i < n; i++) if ((long long)a[i] > m) m = (long long)a[i];
    return m;
}
#endif /* __AVX2__ */

/* ---- Fase 3: pthread parallel reductions ----
 * Particiona [0,n) en N chunks. Cada worker hace una reducción AVX2 sobre su
 * chunk. Main thread combina parciales. Solo activado si n >= TE_RED_PAR_MIN. */
#define TE_RED_PAR_MIN (1u << 17)   /* 131072 elementos: por debajo, serial */
#define TE_RED_PAR_MAX_THR 8

#if TE_HAS_PTHREAD
typedef struct DfRedArg {
    const int64_t *a;
    size_t lo, hi;
    int op;          /* 0=sum 1=min 2=max */
    long long partial_sum;
    long long partial_minmax;
} DfRedArg;

static void *df_red_worker(void *p) {
    DfRedArg *r = (DfRedArg*)p;
    size_t n = r->hi - r->lo;
    const int64_t *base = r->a + r->lo;
    if (r->op == 0) {
#if defined(__AVX2__)
        r->partial_sum = df_sum_i64_avx2(base, n);
#else
        r->partial_sum = df_sum_i64_scalar(base, n);
#endif
    } else if (r->op == 1) {
#if defined(__AVX2__)
        r->partial_minmax = df_min_i64_avx2(base, n);
#else
        r->partial_minmax = df_min_i64_scalar(base, n);
#endif
    } else {
#if defined(__AVX2__)
        r->partial_minmax = df_max_i64_avx2(base, n);
#else
        r->partial_minmax = df_max_i64_scalar(base, n);
#endif
    }
    return NULL;
}
#endif

static long long df_reduce_i64(const int64_t *a, size_t n, int op) {
    if (n == 0) return 0;
#if TE_HAS_PTHREAD
    if (n >= TE_RED_PAR_MIN) {
        int nthr = (int)te_nprocs_online();
        if (nthr < 2) nthr = 2;
        if (nthr > TE_RED_PAR_MAX_THR) nthr = TE_RED_PAR_MAX_THR;
        int max_by_work = (int)(n / 65536u);
        if (max_by_work < 1) max_by_work = 1;
        if (nthr > max_by_work) nthr = max_by_work;
        if (nthr >= 2) {
            pthread_t tids[TE_RED_PAR_MAX_THR];
            DfRedArg args[TE_RED_PAR_MAX_THR];
            size_t chunk = n / (size_t)nthr;
            for (int i = 0; i < nthr; i++) {
                args[i].a = a;
                args[i].lo = (size_t)i * chunk;
                args[i].hi = (i == nthr - 1) ? n : (size_t)(i + 1) * chunk;
                args[i].op = op;
                args[i].partial_sum = 0;
                args[i].partial_minmax = 0;
                pthread_create(&tids[i], NULL, df_red_worker, &args[i]);
            }
            for (int i = 0; i < nthr; i++) pthread_join(tids[i], NULL);
            if (op == 0) {
                long long s = 0;
                for (int i = 0; i < nthr; i++) s += args[i].partial_sum;
                return s;
            } else if (op == 1) {
                long long m = args[0].partial_minmax;
                for (int i = 1; i < nthr; i++) if (args[i].partial_minmax < m) m = args[i].partial_minmax;
                return m;
            } else {
                long long m = args[0].partial_minmax;
                for (int i = 1; i < nthr; i++) if (args[i].partial_minmax > m) m = args[i].partial_minmax;
                return m;
            }
        }
    }
#endif
    if (op == 0) {
#if defined(__AVX2__)
        return df_sum_i64_avx2(a, n);
#else
        return df_sum_i64_scalar(a, n);
#endif
    } else if (op == 1) {
#if defined(__AVX2__)
        return df_min_i64_avx2(a, n);
#else
        return df_min_i64_scalar(a, n);
#endif
    } else {
#if defined(__AVX2__)
        return df_max_i64_avx2(a, n);
#else
        return df_max_i64_scalar(a, n);
#endif
    }
}

/* ---- Fase 1+4: group_sum columnar (int64) ----
 * Agrupa por columna string `kc`, suma columna int64 `vc`.
 * Devuelve DataFrame con 3 cols: [<key>:string, sum:int64, count:int64].
 * Las strings de la key alias-an a las de src (lifetime de proceso, arena CSV). */
/* Hash rápido para keys cortas: mezcla los primeros bytes con multiply-xor.
 * Para keys > 16 bytes cae a FNV-1a tradicional (te_str_hash). */
static inline uint64_t te_short_key_hash(const char *s) {
    uint64_t h0 = 0xcbf29ce484222325ULL;
    /* Lee hasta 16 bytes, mezclando en bloques de 8. */
    size_t i = 0;
    uint64_t lo = 0, hi = 0;
    while (i < 16 && s[i]) {
        if (i < 8) lo |= ((uint64_t)(unsigned char)s[i]) << (i * 8);
        else       hi |= ((uint64_t)(unsigned char)s[i]) << ((i - 8) * 8);
        i++;
    }
    if (i == 16 && s[16]) return te_str_hash(s); /* fallback para >16 bytes */
    /* Mezcla tipo MurmurHash3 finalizer */
    uint64_t h = h0 ^ lo ^ (hi * 0xff51afd7ed558ccdULL) ^ ((uint64_t)i << 56);
    h ^= h >> 33; h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h;
}

/* Partial open-addressing hash for parallel group_sum.
 * Cada hilo construye su propia tabla pequeña sobre un slice de las keys,
 * luego un hilo principal hace merge en la tabla global. Para low-cardinality
 * (10-100 groups) cada thread-local table queda casi vacía y la merge es trivial. */
typedef struct { uint64_t h; const char *k; long long sum; long long count; int used; } GsSlot;

typedef struct {
    char **keys;
    const int64_t *vals;
    int start, end;
    /* v0.0.14 pulimiento #7: tabla per-thread con resize din\u00e1mico
     * (antes era ventana fija de 512 slots → degradaba con >256 groups
     * por probing largo). Ahora cada thread crece su propia tabla y
     * la libera al final. */
    GsSlot *slots;
    size_t cap;      /* potencia de 2 */
    size_t mask;     /* cap - 1 */
    int n_groups;
} GsWorkerArg;

/* Resize x2 + rehash. Llamado cuando n_groups * 2 > cap (load > 50%). */
static void gs_grow(GsWorkerArg *a) {
    size_t new_cap = a->cap * 2;
    size_t new_mask = new_cap - 1;
    GsSlot *new_slots = (GsSlot*)calloc(new_cap, sizeof(GsSlot));
    if (!new_slots) return;  /* OOM: dejar tabla actual; se degradar\u00e1 pero no crash */
    for (size_t i = 0; i < a->cap; i++) {
        if (!a->slots[i].used) continue;
        size_t idx = (size_t)a->slots[i].h & new_mask;
        while (new_slots[idx].used) idx = (idx + 1) & new_mask;
        new_slots[idx] = a->slots[i];
    }
    free(a->slots);
    a->slots = new_slots;
    a->cap = new_cap;
    a->mask = new_mask;
}

static void *df_group_sum_worker(void *p) {
    GsWorkerArg *a = (GsWorkerArg*)p;
    char **keys = a->keys;
    const int64_t *vals = a->vals;
    int n_groups = a->n_groups;
    for (int i = a->start; i < a->end; i++) {
        /* Check load BEFORE insert para garantizar slot libre. */
        if ((size_t)(n_groups + 1) * 2 > a->cap) {
            a->n_groups = n_groups;
            gs_grow(a);
        }
        GsSlot *slots = a->slots;
        size_t mask = a->mask;
        const char *k = keys[i] ? keys[i] : "";
        uint64_t h = te_short_key_hash(k);
        size_t idx = (size_t)h & mask;
        for (;;) {
            if (!slots[idx].used) {
                slots[idx].used = 1;
                slots[idx].h = h;
                slots[idx].k = k;
                slots[idx].sum = (long long)vals[i];
                slots[idx].count = 1;
                n_groups++;
                break;
            }
            if (slots[idx].h == h && strcmp(slots[idx].k, k) == 0) {
                slots[idx].sum += (long long)vals[i];
                slots[idx].count++;
                break;
            }
            idx = (idx + 1) & mask;
        }
    }
    a->n_groups = n_groups;
    return NULL;
}

static DataFrame *df_group_sum(const DataFrame *src, int kc, int vc) {
    int n = src->row_count;
    char **keys = (char**)src->col_data[kc];
    const int64_t *vals = (const int64_t*)src->col_data[vc];

    /* Decisión paralela vs serial: paralelo cuando n >= 50K y hay pthreads. */
#if TE_HAS_PTHREAD
    long ncpu = te_nprocs_online();
    int nthr = (int)ncpu;
    const char *thr_env = getenv("TE_CSV_THREADS");
    if (thr_env) { int v = atoi(thr_env); if (v > 0) nthr = v; }
    if (nthr > 16) nthr = 16;
    if (nthr < 1) nthr = 1;
    int do_parallel = (n >= 50000 && nthr >= 2);
#else
    int do_parallel = 0;
    int nthr = 1;
#endif

    GsSlot *gslots = NULL;
    size_t gcap = 0, gmask = 0;
    int n_groups = 0;

    if (do_parallel) {
#if TE_HAS_PTHREAD
        /* v0.0.14 pulimiento #7: tabla per-thread independiente con resize
         * din\u00e1mico. Cap inicial 1024 (load <50% para hasta 512 groups);
         * crece x2 cuando se llena. Para low-card (10 groups, 8 threads)
         * total = 8 \u00d7 1024 \u00d7 32B = 256KB (cabe c\u00f3modo en L2). Para
         * high-card (50K groups) la tabla crece autom\u00e1ticamente a ~128K
         * slots por thread → 32MB total, igual viable. */
        size_t init_cap = 1024;

        GsWorkerArg *args = (GsWorkerArg*)calloc((size_t)nthr, sizeof(GsWorkerArg));
        pthread_t *tids = (pthread_t*)malloc((size_t)nthr * sizeof(pthread_t));
        int chunk = (n + nthr - 1) / nthr;
        for (int t = 0; t < nthr; t++) {
            args[t].keys = keys;
            args[t].vals = vals;
            args[t].start = t * chunk;
            args[t].end = (t == nthr - 1) ? n : (t + 1) * chunk;
            args[t].slots = (GsSlot*)calloc(init_cap, sizeof(GsSlot));
            args[t].cap = init_cap;
            args[t].mask = init_cap - 1;
            args[t].n_groups = 0;
            pthread_create(&tids[t], NULL, df_group_sum_worker, &args[t]);
        }
        for (int t = 0; t < nthr; t++) pthread_join(tids[t], NULL);

        /* Merge: global cap = next_pow2(suma_groups * 4). */
        int sum_groups = 0;
        for (int t = 0; t < nthr; t++) sum_groups += args[t].n_groups;
        gcap = 16;
        while (gcap < (size_t)sum_groups * 4 && gcap < (1u << 24)) gcap <<= 1;
        gmask = gcap - 1;
        gslots = (GsSlot*)calloc(gcap, sizeof(GsSlot));
        for (int t = 0; t < nthr; t++) {
            GsSlot *ts = args[t].slots;
            for (size_t i = 0; i < args[t].cap; i++) {
                if (!ts[i].used) continue;
                size_t idx = (size_t)ts[i].h & gmask;
                for (;;) {
                    if (!gslots[idx].used) {
                        gslots[idx] = ts[i];
                        n_groups++;
                        break;
                    }
                    if (gslots[idx].h == ts[i].h && strcmp(gslots[idx].k, ts[i].k) == 0) {
                        gslots[idx].sum += ts[i].sum;
                        gslots[idx].count += ts[i].count;
                        break;
                    }
                    idx = (idx + 1) & gmask;
                }
            }
            free(args[t].slots);
        }
        free(args); free(tids);
#endif
    } else {
        /* Serial: tabla global directa. */
        gcap = 16;
        while (gcap < (size_t)n * 2 && gcap < (1u << 28)) gcap <<= 1;
        gmask = gcap - 1;
        gslots = (GsSlot*)calloc(gcap, sizeof(GsSlot));
        if (!gslots) return NULL;
        for (int i = 0; i < n; i++) {
            const char *k = keys[i] ? keys[i] : "";
            uint64_t h = te_short_key_hash(k);
            size_t idx = (size_t)h & gmask;
            for (;;) {
                if (!gslots[idx].used) {
                    gslots[idx].used = 1;
                    gslots[idx].h = h;
                    gslots[idx].k = k;
                    gslots[idx].sum = (long long)vals[i];
                    gslots[idx].count = 1;
                    n_groups++;
                    break;
                }
                if (gslots[idx].h == h && strcmp(gslots[idx].k, k) == 0) {
                    gslots[idx].sum += (long long)vals[i];
                    gslots[idx].count++;
                    break;
                }
                idx = (idx + 1) & gmask;
            }
        }
    }

    DataFrame *out = (DataFrame*)calloc(1, sizeof(DataFrame));
    out->row_count = n_groups;
    out->col_count = 3;
    out->col_kinds = (int*)malloc(3 * sizeof(int));
    out->col_kinds[0] = 1; out->col_kinds[1] = 0; out->col_kinds[2] = 0;
    out->col_data = (void**)calloc(3, sizeof(void*));
    out->col_data[0] = malloc((size_t)n_groups * sizeof(char*));
    out->col_data[1] = malloc((size_t)n_groups * sizeof(int64_t));
    out->col_data[2] = malloc((size_t)n_groups * sizeof(int64_t));
    out->col_names = (char**)malloc(3 * sizeof(char*));
    out->col_names[0] = strdup(src->col_names[kc]);
    out->col_names[1] = strdup("sum");
    out->col_names[2] = strdup("count");
    out->cls = NULL;

    char **ok = (char**)out->col_data[0];
    int64_t *os = (int64_t*)out->col_data[1];
    int64_t *oc = (int64_t*)out->col_data[2];
    int j = 0;
    for (size_t i = 0; i < gcap; i++) {
        if (gslots[i].used) {
            ok[j] = (char*)gslots[i].k;
            os[j] = (int64_t)gslots[i].sum;
            oc[j] = (int64_t)gslots[i].count;
            j++;
        }
    }
    free(gslots);
    return out;
}

/* Despacha métodos sobre wrappers DataFrame: count/sum/min/max/group_sum/print.
 * Devuelve 1 si manejó la llamada (resultado en __ret__), 0 si no. */
int te_df_dispatch_method(DataFrame *df, ASTNode *node) {
    if (!df || !node || !node->id) return 0;
    const char *m = node->id;
    ASTNode *arg1 = node->right;
    ASTNode *arg2 = arg1 ? arg1->right : NULL;
    const char *s1 = (arg1 && arg1->str_value) ? arg1->str_value : NULL;
    const char *s2 = (arg2 && arg2->str_value) ? arg2->str_value : NULL;

    if (strcmp(m, "count") == 0 && !arg1) {
        add_or_update_variable("__ret__",
            create_ast_leaf_number("INT", df->row_count, NULL, NULL));
        return 1;
    }
    if (s1 && (strcmp(m, "sum") == 0 || strcmp(m, "min") == 0 || strcmp(m, "max") == 0)) {
        int ci = df_col_index(df, s1);
        if (ci < 0 || df->col_kinds[ci] != 0) return 0; /* fallback */
        const int64_t *col = (const int64_t*)df->col_data[ci];
        size_t n = (size_t)df->row_count;
        int op = (m[0] == 's') ? 0 : (m[1] == 'i' ? 1 : 2);
        long long t0 = 0;
        if (getenv("TE_CSV_TIMING")) {
            struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
            t0 = (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
        }
        long long r = df_reduce_i64(col, n, op);
        if (getenv("TE_CSV_TIMING")) {
            struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
            long long t1 = (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
            fprintf(stderr, "[DF-OP] %s(%s) %lldus n=%zu\n", m, s1, (t1 - t0) / 1000, n);
        }
        /* TE INT es i32; si el resultado overflowea, devolvemos FLOAT
         * (double exacto hasta 2^53 ~ 9e15). Igual semántica que sumBy de OO mode. */
        if (r >= INT_MIN && r <= INT_MAX) {
            add_or_update_variable("__ret__",
                create_ast_leaf_number("INT", (int)r, NULL, NULL));
        } else {
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", r);
            add_or_update_variable("__ret__",
                create_ast_leaf_number("FLOAT", 0, buf, NULL));
        }
        return 1;
    }
    if (strcmp(m, "group_sum") == 0 && s1 && s2) {
        int kc = df_col_index(df, s1);
        int vc = df_col_index(df, s2);
        if (kc < 0 || vc < 0) return 0;
        if (df->col_kinds[kc] != 1 || df->col_kinds[vc] != 0) return 0;
        long long t0 = 0;
        if (getenv("TE_CSV_TIMING")) {
            struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
            t0 = (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
        }
        DataFrame *res = df_group_sum(df, kc, vc);
        if (getenv("TE_CSV_TIMING")) {
            struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
            long long t1 = (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
            fprintf(stderr, "[DF-OP] group_sum(%s,%s) %lldus n=%d groups=%d\n",
                    s1, s2, (t1 - t0) / 1000, df->row_count, res ? res->row_count : 0);
        }
        if (!res) return 0;
        add_or_update_variable("__ret__", te_df_wrap(res));
        return 1;
    }
    if (strcmp(m, "show") == 0 && !arg1) {
        /* Dump simple: header + hasta 20 filas */
        for (int c = 0; c < df->col_count; c++) {
            if (c) printf("\t");
            printf("%s", df->col_names[c] ? df->col_names[c] : "?");
        }
        printf("\n");
        int limit = df->row_count < 20 ? df->row_count : 20;
        for (int i = 0; i < limit; i++) {
            for (int c = 0; c < df->col_count; c++) {
                if (c) printf("\t");
                if (df->col_kinds[c] == 0) {
                    printf("%lld", (long long)((int64_t*)df->col_data[c])[i]);
                } else {
                    const char *s = ((char**)df->col_data[c])[i];
                    printf("%s", s ? s : "");
                }
            }
            printf("\n");
        }
        if (df->row_count > limit) printf("... (%d more rows)\n", df->row_count - limit);
        add_or_update_variable("__ret__", create_ast_leaf_number("INT", df->row_count, NULL, NULL));
        return 1;
    }
    if ((strcmp(m, "toList") == 0 || strcmp(m, "toArray") == 0) && !arg1) {
        /* Materialize the columnar DataFrame into a real LIST of ObjectNodes.
         * O(N) single-pass: build attrs from cols, chain via ->next, populate
         * TEListIdx in one go (avoids O(N^2) of repeated append_to_list). */
        ClassNode *cls = df->cls;
        if (!cls) return 0;
        int nattr = cls->attr_count;
        int N = df->row_count;
        long long t0 = 0;
        if (getenv("TE_CSV_TIMING")) {
            struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
            t0 = (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
        }
        /* Resolve attr_idx -> col_idx mapping by name. -1 if no match. */
        int *attr_to_col = (int*)malloc((size_t)nattr * sizeof(int));
        if (!attr_to_col) return 0;
        for (int a = 0; a < nattr; a++) {
            attr_to_col[a] = -1;
            const char *aname = cls->attributes[a].id;
            if (!aname) continue;
            for (int c = 0; c < df->col_count; c++) {
                if (df->col_names[c] && strcmp(df->col_names[c], aname) == 0) {
                    attr_to_col[a] = c; break;
                }
            }
        }
        /* Build LIST root + TEListIdx in one pass. */
        ASTNode *list = (ASTNode*)calloc(1, sizeof(ASTNode));
        list->type = strdup("LIST");
        TEListIdx *ix = (TEListIdx*)calloc(1, sizeof(TEListIdx));
        int cap = N < 8 ? 8 : N;
        ix->items = (ASTNode**)calloc((size_t)cap, sizeof(ASTNode*));
        ix->cap = cap;
        ix->len = N;
        list->extra = (struct ASTNode*)ix;
        ASTNode *prev = NULL;
        /* Bulk-allocate N ObjectNodes from arena (single mega-alloc instead
         * of 5+ mallocs per row). ASTNode wrappers come from ast_pool_alloc
         * (block allocator). Per-row mallocs drop from ~9 to ~2 (strdup
         * "OBJECT" + strdup string value). */
        ObjectNode *objs = create_objects_bulk(cls, N);
        for (int i = 0; i < N; i++) {
            ObjectNode *obj = &objs[i];
            for (int a = 0; a < nattr; a++) {
                int ci = attr_to_col[a];
                if (ci < 0) continue;
                Variable *v = &obj->attributes[a];
                if (df->col_kinds[ci] == 0) {
                    int64_t iv = ((int64_t*)df->col_data[ci])[i];
                    if (v->vtype == VAL_INT) {
                        v->value.int_value = (int)iv;
                    } else if (v->vtype == VAL_FLOAT) {
                        v->value.float_value = (double)iv;
                    } else { /* string fallback */
                        char buf[32]; snprintf(buf, sizeof(buf), "%lld", (long long)iv);
                        /* v->value.string_value is NULL from bulk alloc, no free needed. */
                        v->value.string_value = strdup(buf);
                    }
                } else {
                    const char *s = ((const char**)df->col_data[ci])[i];
                    if (v->vtype == VAL_STRING) {
                        /* v->value.string_value is NULL from bulk alloc, no free needed. */
                        v->value.string_value = strdup(s ? s : "");
                    }
                }
            }
            /* ASTNode wrapper from pool (no malloc); type still strdup'd
             * (arena unsafe per ast_pool_alloc comment). */
            ASTNode *on = ast_pool_alloc();
            on->type = strdup("OBJECT");
            on->value = (int)(intptr_t)obj;
            on->extra = (struct ASTNode*)obj;
            ix->items[i] = on;
            if (prev) prev->next = on; else list->left = on;
            prev = on;
        }
        free(attr_to_col);
        if (getenv("TE_CSV_TIMING")) {
            struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
            long long t1 = (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
            fprintf(stderr, "[DF-OP] toList %lldus n=%d\n", (t1 - t0) / 1000, N);
        }
        add_or_update_variable("__ret__", list);
        return 1;
    }
    return 0;
}

/* Construye un DataFrame.
 *
 * Algoritmo: Phase A (spinwait, no futex) → prefix-sum → Phase B directo.
 * Elimina el doble pthread_barrier_wait (~10ms en Docker/WSL2) usando
 * atomic spinwait (<1μs latencia) para coordinar Phase A → Phase B.
 */
static DataFrame *csv_build_dataframe(char *src, size_t len, size_t pos,
                                       ClassNode *cls,
                                       int *attr_kind, int *attr_nullable,
                                       int *col_to_attr,
                                       char **header, int header_n,
                                       int n_workers,
                                       struct timespec *ts_after_count_out,
                                       int readonly_src) {
    int nattr = cls->attr_count;
    for (int a = 0; a < nattr; a++) {
        if (attr_kind[a] != 0 && attr_kind[a] != 1) return NULL;
        if (attr_nullable[a]) return NULL;
    }
    (void)header;
    if (n_workers < 1) n_workers = 1;

    CSVColWorkerArgs *args = (CSVColWorkerArgs*)calloc(n_workers, sizeof(CSVColWorkerArgs));
    size_t data_len = len - pos;
    for (int w = 0; w < n_workers; w++) {
        args[w].src         = src;
        args[w].total_len   = len;
        args[w].chunk_start = pos + (size_t)w * (data_len / n_workers);
        args[w].chunk_end   = (w == n_workers - 1) ? len
                              : pos + (size_t)(w + 1) * (data_len / n_workers);
        args[w].is_first    = (w == 0);
        args[w].nattr       = nattr;
        args[w].header_n    = header_n;
        args[w].attr_kind   = attr_kind;
        args[w].col_to_attr = col_to_attr;
        args[w].phase_a_done = 0;
        args[w].go_phase_b   = 0;
        args[w].readonly_src = readonly_src;
    }

#if TE_HAS_PTHREAD
    /* --- POOL PATH (DESHABILITADO): los threads spinning del pool consumen
     *     todos los cores y starvan al main durante el trabajo serial previo
     *     (has_quote scan, header parse). Net negativo. Mantenido el código
     *     para experimentación futura, pero el if siempre es false. --- */
    int pool_n = __atomic_load_n(&g_csv_pool_n, __ATOMIC_ACQUIRE);
    if (0 && pool_n > 0 && n_workers >= 2) {
        int nw = pool_n < n_workers - 1 ? pool_n : n_workers - 1;
        int n_total = nw + 1;  /* nw pool workers + 1 main */

        /* Rechazar chunks con data_len 0. */
        if (data_len == 0) { free(args); return NULL; }

        /* Llenar slots del pool para workers 1..nw (chunk 0 = main). */
        for (int w = 0; w < nw; w++) {
            size_t w_start = pos + (size_t)(w + 1) * (data_len / n_total);
            size_t w_end   = (w + 1 == nw) ? len
                             : pos + (size_t)(w + 2) * (data_len / n_total);
            g_csv_slots[w].src          = src;
            g_csv_slots[w].total_len    = len;
            g_csv_slots[w].chunk_start  = w_start;
            g_csv_slots[w].chunk_end    = w_end;
            g_csv_slots[w].is_first     = 0;
            g_csv_slots[w].nattr        = nattr;
            g_csv_slots[w].header_n     = header_n;
            g_csv_slots[w].attr_kind    = attr_kind;
            g_csv_slots[w].col_to_attr  = col_to_attr;
            g_csv_slots[w].readonly_src = readonly_src;
            g_csv_slots[w].global_col_data = NULL;
        }
        size_t main_chunk_end = pos + data_len / n_total;

        /* Dispatch Phase A a los pool workers (ya corriendo → latencia ~1μs). */
        for (int w = 0; w < nw; w++)
            __atomic_store_n(&g_csv_slots[w].pool_phase, CSV_POOL_PHASE_A, __ATOMIC_RELEASE);

        /* Main: Phase A para chunk 0 (inline, is_first → no boundary adjust). */
        int main_row_count = (int)csv_count_newlines(src, pos, main_chunk_end);

        /* Spinwait para pool workers. */
        for (int w = 0; w < nw; w++)
            while (__atomic_load_n(&g_csv_slots[w].pool_phase, __ATOMIC_ACQUIRE) != CSV_POOL_IDLE)
                cpu_relax();
        if (ts_after_count_out) clock_gettime(CLOCK_MONOTONIC, ts_after_count_out);

        /* Prefix-sum. */
        int total_rows = main_row_count;
        for (int w = 0; w < nw; w++) {
            g_csv_slots[w].row_offset = total_rows;
            total_rows += g_csv_slots[w].row_count;
        }

        if (total_rows <= 0) { free(args); return NULL; }

        /* Alocar DataFrame con tamaño exacto. */
        DataFrame *df = (DataFrame*)calloc(1, sizeof(DataFrame));
        df->col_count = nattr; df->cls = cls;
        df->col_kinds = (int*)malloc(nattr * sizeof(int));
        df->col_data  = (void**)calloc(nattr, sizeof(void*));
        df->col_names = (char**)malloc(nattr * sizeof(char*));
        for (int a = 0; a < nattr; a++) {
            df->col_kinds[a] = attr_kind[a];
            df->col_names[a] = cls->attributes[a].id;
            size_t slot = (attr_kind[a] == 0) ? sizeof(int64_t) : sizeof(char*);
            df->col_data[a] = malloc((size_t)total_rows * slot);
        }

        /* Propagar global_col_data a pool workers. */
        for (int w = 0; w < nw; w++)
            g_csv_slots[w].global_col_data = df->col_data;

        /* Dispatch Phase B a los pool workers. */
        for (int w = 0; w < nw; w++)
            __atomic_store_n(&g_csv_slots[w].pool_phase, CSV_POOL_PHASE_B, __ATOMIC_RELEASE);

        /* Main: Phase B para chunk 0 (en paralelo con pool workers). */
        {
            CSVColWorkerArgs ma = {0};
            ma.src               = src;
            ma.total_len         = len;
            ma.actual_parse_start = pos;  /* is_first, no boundary */
            ma.chunk_end         = main_chunk_end;
            ma.header_n          = header_n;
            ma.attr_kind         = attr_kind;
            ma.col_to_attr       = col_to_attr;
            ma.global_col_data   = df->col_data;
            ma.row_offset        = 0;
            ma.readonly_src      = readonly_src;
            csv_do_phase_b(&ma);
            main_row_count = ma.row_count;
        }

        /* Spinwait hasta que todos los pool workers terminen Phase B. */
        for (int w = 0; w < nw; w++)
            while (__atomic_load_n(&g_csv_slots[w].pool_phase, __ATOMIC_ACQUIRE) != CSV_POOL_IDLE)
                cpu_relax();

        int total_actual = main_row_count;
        for (int w = 0; w < nw; w++) total_actual += g_csv_slots[w].row_count;
        df->row_count = total_actual;
        free(args);
        return df;
    }

    /* --- FALLBACK: pool no inicializado → crear threads (pthread_create). --- */
    if (n_workers >= 2) {
        /* Patrón "main-as-worker-0": solo crea (n_workers-1) threads. */
        int nw = n_workers - 1;
        pthread_t *tids = (pthread_t*)malloc(nw * sizeof(pthread_t));
        for (int w = 1; w <= nw; w++)
            pthread_create(&tids[w-1], NULL, csv_combined_col_worker, &args[w]);

        /* Main: Phase A para chunk 0. */
        {
            size_t p2 = args[0].chunk_start;
            args[0].actual_parse_start = p2;
            args[0].row_count = (int)csv_count_newlines(src, p2, args[0].chunk_end);
        }

        for (int w = 1; w < n_workers; w++)
            while (!__atomic_load_n(&args[w].phase_a_done, __ATOMIC_ACQUIRE))
                cpu_relax();
        if (ts_after_count_out) clock_gettime(CLOCK_MONOTONIC, ts_after_count_out);

        int total_rows = 0;
        for (int w = 0; w < n_workers; w++) {
            args[w].row_offset = total_rows;
            total_rows += args[w].row_count;
        }

        DataFrame *df = NULL;
        if (total_rows > 0) {
            df = (DataFrame*)calloc(1, sizeof(DataFrame));
            df->col_count = nattr; df->cls = cls;
            df->col_kinds = (int*)malloc(nattr * sizeof(int));
            df->col_data  = (void**)calloc(nattr, sizeof(void*));
            df->col_names = (char**)malloc(nattr * sizeof(char*));
            for (int a = 0; a < nattr; a++) {
                df->col_kinds[a] = attr_kind[a];
                df->col_names[a] = cls->attributes[a].id;
                size_t slot = (attr_kind[a] == 0) ? sizeof(int64_t) : sizeof(char*);
                df->col_data[a] = malloc((size_t)total_rows * slot);
            }
            for (int w = 0; w < n_workers; w++)
                args[w].global_col_data = df->col_data;
        }

        for (int w = 1; w < n_workers; w++)
            __atomic_store_n(&args[w].go_phase_b, 1, __ATOMIC_RELEASE);

        args[0].go_phase_b = 1;
        if (df) csv_combined_col_worker(&args[0]);

        for (int w = 0; w < nw; w++) pthread_join(tids[w], NULL);
        free(tids);

        if (!df) { free(args); return NULL; }

        int total_actual = 0;
        for (int w = 0; w < n_workers; w++) total_actual += args[w].row_count;
        df->row_count = total_actual;
        free(args);
        return df;
    }
#endif /* TE_HAS_PTHREAD */

    /* Serial (n_workers==1 o sin pthread). */
    {
        size_t p2 = args[0].chunk_start;
        if (!args[0].is_first && p2 > 0 && src[p2-1] != '\n') {
            while (p2 < len && src[p2] != '\n') p2++;
            if (p2 < len) p2++;
        }
        args[0].actual_parse_start = p2;
        args[0].row_count = (int)csv_count_newlines(src, p2, args[0].chunk_end);
        if (ts_after_count_out) clock_gettime(CLOCK_MONOTONIC, ts_after_count_out);

        int total_rows = args[0].row_count;
        if (total_rows <= 0) { free(args); return NULL; }

        DataFrame *df = (DataFrame*)calloc(1, sizeof(DataFrame));
        df->col_count = nattr; df->cls = cls;
        df->col_kinds = (int*)malloc(nattr * sizeof(int));
        df->col_data  = (void**)calloc(nattr, sizeof(void*));
        df->col_names = (char**)malloc(nattr * sizeof(char*));
        for (int a = 0; a < nattr; a++) {
            df->col_kinds[a] = attr_kind[a];
            df->col_names[a] = cls->attributes[a].id;
            size_t slot = (attr_kind[a] == 0) ? sizeof(int64_t) : sizeof(char*);
            df->col_data[a] = malloc((size_t)total_rows * slot);
        }
        args[0].row_offset = 0;
        args[0].global_col_data = df->col_data;
        args[0].go_phase_b = 1;   /* auto-señal para serial */
        csv_combined_col_worker(&args[0]); /* Phase B serial */
        df->row_count = args[0].row_count;
        free(args);
        return df;
    }
}

typedef struct CSVParseCfg {
    ClassNode *cls;
    int nattr;
    int header_n;
    int *attr_kind;
    int *attr_nullable;
    int *col_to_attr;
    char **shared_attr_id;
    char **shared_attr_type;
    char *null_type;
    char *shared_class_name;
    char *shared_obj_type;
    char **header_for_errors;
    const char *filename_for_errors;
    /* Pre-built Variable[nattr] template (id/type/vtype/is_const set, value zeroed).
     * Workers memcpy esto a obj->attributes en lugar del init-loop nattr*6 writes. */
    Variable *row_template;
    size_t row_template_bytes; /* nattr * sizeof(Variable) */
} CSVParseCfg;

typedef struct CSVWorkerArgs {
    const CSVParseCfg *cfg;
    char *src;
    size_t total_len;
    size_t chunk_start;
    size_t chunk_end;
    int is_first;
    /* outputs */
    ASTNode *first;
    ASTNode *last;
    CSVChunk *arena_head;
    ASTNodePool *pool_head;
    /* v0.0.13 (perf): escritura DIRECTA al colcache global durante el
     * parseo. Se pre-cuenta filas por chunk (csv_count_newlines, AVX2),
     * se hace prefix-sum → row_offset, y se asignan punteros gcol_X a
     * los arrays globales. Workers escriben a gcol_i[k][row_offset + i]
     * sin malloc por-worker ni memcpy posterior. */
    int       row_offset;       /* prefix-sum offset en arrays globales */
    int       wrow_count;       /* filas efectivamente parseadas */
    int64_t **gcol_i;           /* [nattr] punteros a arrays globales (NULL si no aplica) */
    double  **gcol_f;           /* [nattr] */
    const char ***gcol_s;       /* [nattr] */
    ASTNode  **gitems;          /* puntero a items[] global (escribimos gitems[row_offset+i]) */
} CSVWorkerArgs;

/* Parsea un rango. Usa thread-local arena/pool. Devuelve sub-lista vía
 * out->first / out->last.
 *
 * v0.0.13 (perf): si out->gcol_i / out->gcol_f / out->gcol_s / out->gitems
 * apuntan a arrays globales (pre-asignados tras el pre-count de filas),
 * escribimos directamente al colcache global en gcol_X[k][row_offset+i],
 * eliminando el segundo recorrido de te_colcache_build (~650 ms en 10M
 * filas) sin malloc por-worker ni memcpy posterior. */
static void csv_parse_chunk(const CSVParseCfg *cfg, char *src, size_t total_len,
                             size_t start, size_t end, int is_first,
                             CSVWorkerArgs *out) {
    int header_n = cfg->header_n;
    int nattr = cfg->nattr;
    const int *attr_kind = cfg->attr_kind;
    const int *attr_nullable = cfg->attr_nullable;
    const int *col_to_attr = cfg->col_to_attr;
    char **shared_attr_id = cfg->shared_attr_id;
    char **shared_attr_type = cfg->shared_attr_type;
    char *null_type = cfg->null_type;
    char *shared_class_name = cfg->shared_class_name;
    char **header = cfg->header_for_errors;
    (void)shared_attr_id;
    (void)shared_attr_type;

    /* v0.0.14 (perf): two modes for direct-write:
     *   - build_cols + gitems != NULL → write to colcache cols AND wrappers (legacy)
     *   - build_cols + gitems == NULL → pure columnar (skip wrappers entirely)
     * Detect pure_col via gcol_i/gcol_s present but gitems absent. */
    int build_cols = (out && (out->gcol_i != NULL || out->gcol_s != NULL));
    int row_offset = out ? out->row_offset : 0;
    int has_items = (out && out->gitems != NULL);

    size_t pos = start;
    if (!is_first) {
        /* Si chunk_start cae JUSTO al inicio de una fila (el byte anterior
         * es '\n'), entonces nuestra primera fila empieza EN start \u2014 no hay
         * fila parcial que saltar. Si saltáramos, perderíamos esa fila
         * (el worker previo paró en row_begin >= end == start, y no la
         * procesó). Sólo saltar si estamos a mitad de fila. */
        if (pos > 0 && src[pos - 1] != '\n') {
            /* Buscar SIEMPRE en [pos, total_len), no sólo en [pos, end),
             * porque el primer '\n' puede estar dentro del próximo chunk. */
            while (pos < total_len && src[pos] != '\n') pos++;
            if (pos < total_len) pos++;
        }
    }

    char **rec_fields = (char**)malloc(header_n * sizeof(char*));
    ASTNode *first = NULL, *last = NULL;
    int row_idx = 0;

    /* PROFILE: TE_CSV_SCAN_ONLY=1 → solo invoca el scanner y descarta el resto.
     * Mide costo puro del byte scanner (csv_next_record_zerocopy) sin
     * ObjectNode/Variable[]/ast_pool/colcache writes. */
    static int scan_only = -1;
    if (scan_only < 0) scan_only = getenv("TE_CSV_SCAN_ONLY") ? 1 : 0;

    /* v0.0.14 (perf, OPT-IN): TE_CSV_COLUMNAR=1 → modo columnar puro.
     * Activado cuando build_cols=1 Y has_items=0 (el caller decidió no
     * construir el array de wrappers — desde from_csv_to_list cuando
     * detecta el env). En este modo:
     * - SKIP allocación de ObjectNode + Variable[] + memcpy template
     * - SKIP escrituras a obj->attributes
     * - SKIP ast_pool_alloc (10M wrappers) + linked list (~640MB writes)
     * - SKIP loop de validación
     * - Parsea cada campo y escribe DIRECTO al colcache global (gcol_i/gcol_s)
     * COSTO: iteración OO (`for p in productos { p.precio }`) falla porque
     * no hay wrappers. list.length viene del col_cache->n_rows. Para CSV
     * grandes usados como dataframes esto es aceptable. */
    int pure_col = (build_cols && !has_items);

    while (pos < end) {
        size_t row_begin = pos;
        int rn = csv_next_record_zerocopy(src, total_len, &pos, rec_fields, header_n);
        if (rn <= 0) break;
        /* Si la fila comenzó en/después de end, no es nuestra. */
        if (row_begin >= end) break;
        row_idx++;

        if (rn == 1 && rec_fields[0][0] == '\0') continue;
        if (scan_only) continue;

        char **rec = rec_fields;

        if (pure_col) {
            /* ===== FAST PATH COLUMNAR PURO (sin wrappers) ===== */
            int r = row_offset + out->wrow_count;
            int upto = rn < header_n ? rn : header_n;
            for (int c = 0; c < upto; c++) {
                int a = col_to_attr[c];
                if (a < 0) continue;
                const char *raw = rec[c];

                if (attr_kind[a] == 0 /*INT*/) {
                    long v = 0;
                    if (raw[0] == '\0') {
                        if (!attr_nullable[a]) {
                            fprintf(stderr, "CSVError: fila %d, columna '%s' (int) está vacía.\n",
                                    row_idx, header[c]);
                            exit(1);
                        }
                    } else {
                        const char *p = raw;
                        int neg = 0;
                        if (*p == '-') { neg = 1; p++; }
                        else if (*p == '+') { p++; }
                        int ok = (*p != '\0');
                        while (*p) {
                            unsigned d = (unsigned)(*p - '0');
                            if (d > 9u) { ok = 0; break; }
                            v = v * 10 + (long)d;
                            p++;
                        }
                        if (!ok) {
                            char *endp = NULL;
                            v = strtol(raw, &endp, 10);
                            if (endp == raw || (endp && *endp != '\0')) {
                                fprintf(stderr, "CSVError: fila %d, columna '%s': '%s' no es int.\n",
                                        row_idx, header[c], raw);
                                exit(1);
                            }
                        } else if (neg) {
                            v = -v;
                        }
                    }
                    if (out->gcol_i && out->gcol_i[a])
                        out->gcol_i[a][r] = (int64_t)v;
                } else if (attr_kind[a] == 1 /*STRING*/) {
                    if (raw[0] == '\0' && !attr_nullable[a]) {
                        fprintf(stderr, "CSVError: fila %d: atributo '%s' (no nullable) sin valor.\n",
                                row_idx, cfg->cls->attributes[a].id);
                        exit(1);
                    }
                    if (out->gcol_s && out->gcol_s[a])
                        out->gcol_s[a][r] = (raw[0] == '\0') ? NULL : (char*)raw;
                } else {
                    if (out->gcol_s && out->gcol_s[a])
                        out->gcol_s[a][r] = (char*)raw;
                }
            }
            /* NO wrapper, NO linked list, NO gitems write. list.length
             * vendrá de col_cache->n_rows vía list_length(). */
            out->wrow_count++;
            continue;
        }

        /* ===== PATH LEGACY (compat: ObjectNode + attrs) ===== */
        ObjectNode *obj = (ObjectNode*)csv_arena_alloc(sizeof(ObjectNode));
        obj->class = cfg->cls;
        obj->attributes = (Variable*)csv_arena_alloc(nattr * sizeof(Variable));
        /* Bulk-init via memcpy del template precomputado (id/type/vtype/is_const ya seteados).
         * Reemplaza nattr*6 writes individuales por una memcpy de ~48 bytes (cache hot). */
        memcpy(obj->attributes, cfg->row_template, cfg->row_template_bytes);

        int upto = rn < header_n ? rn : header_n;
        for (int c = 0; c < upto; c++) {
            int a = col_to_attr[c];
            if (a < 0) continue;
            const char *raw = rec[c];

            if (attr_kind[a] == 0 /*INT*/) {
                if (raw[0] == '\0') {
                    if (attr_nullable[a]) {
                        obj->attributes[a].type = null_type;
                        obj->attributes[a].value.int_value = 0;
                    } else {
                        fprintf(stderr,
                            "CSVError: fila %d, columna '%s' (int) está vacía.\n",
                            row_idx, header[c]);
                        exit(1);
                    }
                } else {
                    /* Fast int parser: hot path 100% dígitos ASCII (con signo opcional).
                     * strtol pesa por locale + setear errno + skip whitespace + base detect.
                     * Fallback a strtol si encontramos algo raro. */
                    const char *p = raw;
                    int neg = 0;
                    if (*p == '-') { neg = 1; p++; }
                    else if (*p == '+') { p++; }
                    long v = 0;
                    int ok = (*p != '\0');
                    while (*p) {
                        unsigned d = (unsigned)(*p - '0');
                        if (d > 9u) { ok = 0; break; }
                        v = v * 10 + (long)d;
                        p++;
                    }
                    if (!ok) {
                        char *endp = NULL;
                        v = strtol(raw, &endp, 10);
                        if (endp == raw || (endp && *endp != '\0')) {
                            fprintf(stderr,
                                "CSVError: fila %d, columna '%s': '%s' no es int.\n",
                                row_idx, header[c], raw);
                            exit(1);
                        }
                    } else if (neg) {
                        v = -v;
                    }
                    obj->attributes[a].value.int_value = (int)v;
                }
            } else if (attr_kind[a] == 1 /*STRING*/) {
                if (raw[0] == '\0' && attr_nullable[a]) {
                    obj->attributes[a].type = null_type;
                    obj->attributes[a].value.string_value = NULL;
                } else {
                    /* ZERO-COPY: raw apunta dentro de src (mmap MAP_PRIVATE)
                     * y ya está null-terminado por el scanner. src vive hasta
                     * exit (no se munmap). Saltamos memcpy de N filas × M cols.
                     * RIESGO: si el script reasigna obj.string_attr=X y ese
                     * código hace free(viejo), crash. Aceptable para CSV read-only. */
                    obj->attributes[a].value.string_value = (char*)raw;
                }
            } else {
                obj->attributes[a].vtype = VAL_STRING;
                obj->attributes[a].value.string_value = (char*)raw;
            }
        }

        for (int a = 0; a < nattr; a++) {
            if (attr_kind[a] == 1 /*STRING*/
                && obj->attributes[a].vtype == VAL_STRING
                && obj->attributes[a].value.string_value == NULL) {
                if (attr_nullable[a]) {
                    obj->attributes[a].type = null_type;
                } else {
                    fprintf(stderr,
                        "CSVError: fila %d: atributo '%s' (no nullable) sin valor.\n",
                        row_idx, cfg->cls->attributes[a].id);
                    exit(1);
                }
            }
        }

        /* Wrapper ASTNode desde POOL bump (4096 nodes/block, calloc'd).
         * free_ast respeta `from_pool` y no llama free(node).
         * type = sentinel global g_csv_wrapper_obj_type (free_ast lo skip).
         * id  = shared_class_name + id_interned=1 (free_ast lo skip).
         * Bloques del pool se linkean a g_ast_pool_keepalive y viven al exit. */
        ASTNode *item = ast_pool_alloc();
        item->type = cfg->shared_obj_type;
        item->id = shared_class_name;
        item->id_interned = 1;
        item->from_pool = 1;
        item->extra = (struct ASTNode*)obj;
        item->value = (int)(intptr_t)obj;

        if (!first) { first = last = item; }
        else        { last->next = item; last = item; }

        /* v0.0.13 (perf): escritura DIRECTA al colcache global. Los arrays
         * están pre-asignados (tras pre-count de filas), y row_offset es
         * el prefix-sum del worker. Sin malloc ni memcpy: la única copia
         * es esta escritura, que es write-combining-friendly. */
        if (build_cols) {
            int r = row_offset + out->wrow_count;
            for (int a = 0; a < nattr; a++) {
                if (attr_kind[a] == 0 /*INT*/) {
                    if (out->gcol_i && out->gcol_i[a])
                        out->gcol_i[a][r] = (int64_t)obj->attributes[a].value.int_value;
                } else if (attr_kind[a] == 1 /*STRING*/) {
                    if (out->gcol_s && out->gcol_s[a])
                        out->gcol_s[a][r] = obj->attributes[a].value.string_value;
                }
            }
            out->gitems[r] = item;
            out->wrow_count++;
        }
    }

    free(rec_fields);
    if (out) { out->first = first; out->last = last; }
}

#if TE_HAS_PTHREAD
static void *csv_parse_worker(void *p) {
    CSVWorkerArgs *a = (CSVWorkerArgs*)p;
    /* Reset thread-local arena/pool para esta corrida. */
    t_csv_arena = NULL;
    t_ast_pool = NULL;
    csv_parse_chunk(a->cfg, a->src, a->total_len,
                    a->chunk_start, a->chunk_end, a->is_first, a);
    a->arena_head = t_csv_arena;
    a->pool_head  = t_ast_pool;
    return NULL;
}
#endif

/* Thread-local override: forzar modo columnar (DataFrame) por call site.
 * Activado por la sintaxis `from "x.csv", Clase as dataframe;` vía wrapper
 * from_csv_to_dataframe(). Tiene precedencia sobre la var de entorno. */
static __thread int t_csv_force_df = 0;

ASTNode* from_csv_to_dataframe(const char* filename, ClassNode* cls) {
    t_csv_force_df = 1;
    ASTNode *r = from_csv_to_list(filename, cls);
    t_csv_force_df = 0;
    return r;
}

ASTNode* from_csv_to_list(const char* filename, ClassNode* cls) {
    /* Profiling opcional via env TE_CSV_TIMING=1 */
    const char *te_timing = getenv("TE_CSV_TIMING");
    /* Modo columnar: env TE_CSV_DATAFRAME=1 (global) o sintaxis
     * `from ..., Clase as dataframe;` (per-call vía t_csv_force_df).
     * Skip ObjectNode/Variable/wrapper creation entirely: parsea directo a
     * column buffers tipo Arrow. df.length funciona; iter/index NO. */
    const char *te_df_env = getenv("TE_CSV_DATAFRAME");
    int want_columnar = t_csv_force_df || (te_df_env && atoi(te_df_env) > 0);
    struct timespec ts0, ts_after_mmap, ts_after_header, ts_before_parse, ts_after_parse, ts_end;
    if (te_timing) clock_gettime(CLOCK_MONOTONIC, &ts0);

    size_t len = 0;
    /* Usar pread en lugar de mmap para evitar page-fault overhead en Docker
     * overlay FS / WSL2 9P. Ver comentario en csv_read_file(). */
    int src_is_mmap = 0;
    char *src = csv_read_file(filename, &len, &src_is_mmap);
    if (!src) {
        fprintf(stderr, "IOError: no se pudo abrir/mapear el archivo CSV '%s'.\n", filename);
        exit(1);
    }
    if (te_timing) clock_gettime(CLOCK_MONOTONIC, &ts_after_mmap);

    size_t pos = 0;
    if (len >= 3 && (unsigned char)src[0] == 0xEF
                 && (unsigned char)src[1] == 0xBB
                 && (unsigned char)src[2] == 0xBF) {
        pos = 3;
    }

    int header_n = 0;
    char **header = csv_next_record(src, len, &pos, &header_n);
    if (!header) {
        fprintf(stderr, "CSVError: archivo '%s' está vacío.\n", filename);
        exit(1);
    }

    int nattr = cls->attr_count;

    /* Pre-classify each attribute slot. */
    enum { K_INT = 0, K_STRING = 1, K_OTHER = 2 };
    int *attr_kind = (int*)malloc(nattr * sizeof(int));
    int *attr_nullable = (int*)malloc(nattr * sizeof(int));
    for (int a = 0; a < nattr; a++) {
        const char *t = cls->attributes[a].type;
        attr_kind[a] = csv_attr_is_int(t) ? K_INT
                     : csv_attr_is_string(t) ? K_STRING
                     : K_OTHER;
        attr_nullable[a] = csv_attr_is_nullable(t);
    }

    /* Map columnas CSV -> índice de atributo. */
    int *col_to_attr = (int*)malloc(header_n * sizeof(int));
    for (int c = 0; c < header_n; c++) col_to_attr[c] = -1;
    for (int c = 0; c < header_n; c++) {
        for (int a = 0; a < nattr; a++) {
            if (cls->attributes[a].id && !strcmp(cls->attributes[a].id, header[c])) {
                col_to_attr[c] = a; break;
            }
        }
    }
    for (int a = 0; a < nattr; a++) {
        int found = 0;
        for (int c = 0; c < header_n; c++) if (col_to_attr[c] == a) { found = 1; break; }
        if (!found && !attr_nullable[a]) {
            fprintf(stderr,
                "CSVError: atributo '%s' (clase %s) no tiene columna en '%s'.\n",
                cls->attributes[a].id, cls->name, filename);
            csv_free_record(header, header_n);
            free(col_to_attr); free(attr_kind); free(attr_nullable);
            exit(1);
        }
    }

    /* Pre-cache attribute id/type strings UNA SOLA VEZ por carga, en arena
     * (main thread). Workers usan estos punteros directos (read-only). */
    char **shared_attr_id_arena   = (char**)malloc(nattr * sizeof(char*));
    char **shared_attr_type_arena = (char**)malloc(nattr * sizeof(char*));
    char *null_type_arena = csv_arena_strdup("NULL");
    char *shared_class_name_arena = csv_arena_strdup(cls->name);
    /* Inicializa una sola vez el literal global del type del wrapper. */
    if (!g_csv_wrapper_obj_type) {
        g_csv_wrapper_obj_type = csv_arena_strdup("OBJECT");
    }
    char *shared_obj_type = g_csv_wrapper_obj_type;
    for (int a = 0; a < nattr; a++) {
        shared_attr_id_arena[a]   = csv_arena_strdup(cls->attributes[a].id);
        shared_attr_type_arena[a] = csv_arena_strdup(cls->attributes[a].type);
    }

    /* Row template: prebuild Variable[nattr] que se memcpy-ea por fila. */
    Variable *row_template = (Variable*)csv_arena_alloc(nattr * sizeof(Variable));
    memset(row_template, 0, nattr * sizeof(Variable));
    for (int a = 0; a < nattr; a++) {
        row_template[a].id = shared_attr_id_arena[a];
        row_template[a].type = shared_attr_type_arena[a];
        row_template[a].is_const = 0;
        row_template[a].vtype = (attr_kind[a] == 0 /*INT*/) ? VAL_INT : VAL_STRING;
        /* value bytes ya en cero por memset; suficiente para int=0 y string_value=NULL. */
    }

    /* Config compartida read-only para workers. */
    CSVParseCfg cfg;
    cfg.cls = cls;
    cfg.nattr = nattr;
    cfg.header_n = header_n;
    cfg.attr_kind = attr_kind;
    cfg.attr_nullable = attr_nullable;
    cfg.col_to_attr = col_to_attr;
    cfg.shared_attr_id = shared_attr_id_arena;
    cfg.shared_attr_type = shared_attr_type_arena;
    cfg.null_type = null_type_arena;
    cfg.shared_class_name = shared_class_name_arena;
    cfg.shared_obj_type = shared_obj_type;
    cfg.header_for_errors = header;
    cfg.filename_for_errors = filename;
    cfg.row_template = row_template;
    cfg.row_template_bytes = (size_t)nattr * sizeof(Variable);

    ASTNode *first = NULL, *last = NULL;

    /* v0.0.13 (perf): args[] retenidos hasta después de construir el
     * colcache global (necesitamos los wcol_X y witems por-worker). */
    CSVWorkerArgs *worker_args = NULL;
    int worker_args_n = 0;
    TeColCache *worker_gcache = NULL;

    if (te_timing) clock_gettime(CLOCK_MONOTONIC, &ts_after_header);
    if (te_timing) clock_gettime(CLOCK_MONOTONIC, &ts_before_parse);

    /* Decidir si paralelizar. Threshold: archivo > 256KB y >= 2 cores.
     * Limitación: el splitter por chunks asume que '\n' es siempre fin
     * de fila (no hay newlines dentro de campos quoted). Para CSVs con
     * embedded newlines, forzar serial. Heuristic simple: si hay '"' en
     * el archivo, NO paralelizar (conservador y rápido de testear).
     * (Para nuestro bench productos.csv: sin quotes → paraleliza.) */
    int can_parallel = 0;
#if TE_HAS_PTHREAD
    /* Override por env: TE_CSV_THREADS=N (0 fuerza serial). */
    const char *te_threads_env = getenv("TE_CSV_THREADS");
    int forced = te_threads_env ? atoi(te_threads_env) : -1;
    if (forced == 0) {
        can_parallel = 0;
    } else if (len - pos > (256u * 1024u)) {
        long ncpu = te_nprocs_online();
        if (ncpu < 1) ncpu = 1;
        /* Cap adaptativo según tamaño del archivo.
         * Re-medido mayo 2026 (best-of-30): el sweet spot real es
         * ~1MB de CSV por thread (no 2MB como estimación previa).
         * Mediciones: 200k/3.5MB → óptimo 4 threads; 1M/11.2MB → 8.
         * Cap absoluto a 16 por default; override via env TE_CSV_MAX_THREADS=N
         * para máquinas con muchos cores y archivos grandes (>16MB). */
        long max_threads_cap = 16;
        const char *te_max_env = getenv("TE_CSV_MAX_THREADS");
        if (te_max_env) {
            long v = atol(te_max_env);
            if (v >= 1 && v <= 256) max_threads_cap = v;
        }
        size_t bytes = len - pos;
        long ideal_by_size = (long)(bytes / (1u * 1024u * 1024u));
        if (ideal_by_size < 2) ideal_by_size = 2;
        if (ncpu > ideal_by_size) ncpu = ideal_by_size;
        if (ncpu > max_threads_cap) ncpu = max_threads_cap;
        if (forced > 0) ncpu = forced;
        if (ncpu >= 2) {
            /* Detección rápida de quotes (8 bytes a la vez). */
            int has_quote = 0;
            for (size_t i = pos; i < len; i++) {
                if (src[i] == '"') { has_quote = 1; break; }
            }
            if (!has_quote) can_parallel = (int)ncpu;
        }
    }
#endif

    /* ===== RUTA COLUMNAR (DataFrame). Solo si TE_CSV_DATAFRAME=1 y la
     * clase es elegible (int/string puros). Saltamos la fase row-objects.
     * Importante: usar la MISMA decisión de threads que el path normal. */
    if (want_columnar) {
        int n_workers = can_parallel >= 2 ? can_parallel : 1;
        struct timespec ts_after_count;
        DataFrame *df = csv_build_dataframe(src, len, pos, cls,
                                             attr_kind, attr_nullable, col_to_attr,
                                             header, header_n, n_workers,
                                             te_timing ? &ts_after_count : NULL,
                                             src_is_mmap);
        if (df) {
            if (te_timing) clock_gettime(CLOCK_MONOTONIC, &ts_after_parse);
            csv_free_record(header, header_n);
            free(col_to_attr); free(attr_kind); free(attr_nullable);
            free(shared_attr_id_arena); free(shared_attr_type_arena);
            /* Wrapper LIST con TEListIdx pre-set para que .length sea O(1)
             * sin crear ASTNodes hijos. left=NULL → iter no soportada.
             * v0.0.11-pre: stashea DataFrame* en ix->items con sentinel cap
             * para habilitar métodos analíticos (sum/min/max/group_sum). */
            ASTNode *listNode = te_df_wrap(df);
            if (te_timing) {
                clock_gettime(CLOCK_MONOTONIC, &ts_end);
                long us_mmap   = (ts_after_mmap.tv_sec   - ts0.tv_sec)*1000000L + (ts_after_mmap.tv_nsec   - ts0.tv_nsec)/1000L;
                long us_header = (ts_after_header.tv_sec - ts_after_mmap.tv_sec)*1000000L + (ts_after_header.tv_nsec - ts_after_mmap.tv_nsec)/1000L;
                long us_parse_par = (ts_after_count.tv_sec  - ts_before_parse.tv_sec)*1000000L + (ts_after_count.tv_nsec  - ts_before_parse.tv_nsec)/1000L;
                long us_memcpy    = (ts_after_parse.tv_sec  - ts_after_count.tv_sec)*1000000L + (ts_after_parse.tv_nsec  - ts_after_count.tv_nsec)/1000L;
                long us_total  = (ts_end.tv_sec - ts0.tv_sec)*1000000L + (ts_end.tv_nsec - ts0.tv_nsec)/1000L;
                /* parse = Phase A (count newlines paralelo, SIMD AVX2)
                 * phaseB = Phase B (parse campos + write directo al DataFrame, paralelo) */
                fprintf(stderr, "[CSV-COL] io=%ldus header=%ldus phaseA=%ldus phaseB=%ldus total=%ldus rows=%d\n",
                        us_mmap, us_header, us_parse_par, us_memcpy, us_total, df->row_count);
            }
            return listNode;
        }
        /* Fallback al path normal si la clase no era elegible. */
    }

#if TE_HAS_PTHREAD
    if (can_parallel >= 2) {
        int N = can_parallel;
        pthread_t *tids = (pthread_t*)malloc(N * sizeof(pthread_t));
        CSVWorkerArgs *args = (CSVWorkerArgs*)calloc(N, sizeof(CSVWorkerArgs));

        size_t data_start = pos;
        size_t data_len = len - data_start;
        size_t chunk = data_len / N;

        /* v0.0.13 (perf): camino de escritura DIRECTA al colcache global.
         *  1) Pre-contar filas por chunk (csv_count_newlines, AVX2). El
         *     conteo expandido cubre cada worker hasta el siguiente '\n'
         *     ≥ chunk_end, igualando exactamente lo que csv_parse_chunk
         *     procesará tras ajustar bordes. Cualquier mismatch → desactivar
         *     y caer al pase legacy te_colcache_build.
         *  2) Prefix-sum → row_offset por worker, total_n global.
         *  3) Allocar arrays globales del colcache UNA sola vez (int_cols /
         *     str_cols / items) sin malloc por-worker.
         *  4) Spawnear workers con gcol_X apuntando a globales + row_offset.
         *  5) Al join, attach TeColCache prebuilt (sin recorrer la lista). */
        const char *eco_pre = getenv("TE_COLCACHE");
        int want_colcache = (!eco_pre || eco_pre[0] != '0');
        /* v0.0.14: TE_CSV_COLUMNAR=1 → skip wrappers (items[] NO alocado).
         * Per-call override (g_te_csv_columnar_next) wins over env. Consumed
         * and reset to -1 below so it only affects this single load. */
        int pure_columnar;
        if (g_te_csv_columnar_next >= 0) { pure_columnar = g_te_csv_columnar_next; g_te_csv_columnar_next = -1; }
        else pure_columnar = getenv("TE_CSV_COLUMNAR") ? 1 : 0;
        int prep_ok = want_colcache;
        TeColCache *gcache = NULL;
        int total_n = 0;
        int64_t **gcol_i = NULL;
        const char ***gcol_s = NULL;
        ASTNode **gitems = NULL;

        for (int i = 0; i < N; i++) {
            args[i].cfg = &cfg;
            args[i].src = src;
            args[i].total_len = len;
            args[i].chunk_start = data_start + (size_t)i * chunk;
            args[i].chunk_end   = (i == N - 1) ? len : (data_start + (size_t)(i + 1) * chunk);
            args[i].is_first    = (i == 0);
        }

        if (prep_ok) {
            int offset = 0;
            for (int i = 0; i < N; i++) {
                size_t adj_start = args[i].chunk_start;
                if (!args[i].is_first && adj_start > 0 && src[adj_start-1] != '\n') {
                    while (adj_start < len && src[adj_start] != '\n') adj_start++;
                    if (adj_start < len) adj_start++;
                }
                size_t end_inclusive = args[i].chunk_end;
                if (i < N - 1) {
                    if (end_inclusive > 0 && end_inclusive < len && src[end_inclusive-1] != '\n') {
                        while (end_inclusive < len && src[end_inclusive] != '\n') end_inclusive++;
                        if (end_inclusive < len) end_inclusive++;
                    }
                }
                int rc = (adj_start < end_inclusive)
                       ? (int)csv_count_newlines(src, adj_start, end_inclusive)
                       : 0;
                args[i].row_offset = offset;
                args[i].wrow_count = 0; /* worker incrementa */
                offset += rc;
            }
            total_n = offset;
            if (total_n > 0) {
                gcache = (TeColCache*)calloc(1, sizeof(TeColCache));
                gcache->cls = cls;
                gcache->n_rows = total_n;
                gcache->nattr = nattr;
                gcache->kinds = (int*)calloc(nattr, sizeof(int));
                gcache->int_cols = (int64_t**)calloc(nattr, sizeof(int64_t*));
                gcache->flt_cols = (double**)calloc(nattr, sizeof(double*));
                gcache->str_cols = (const char***)calloc(nattr, sizeof(const char**));
                /* pure_columnar: NO alocar items — ahorro 80MB writes en 10M filas. */
                gcache->items = pure_columnar
                    ? NULL
                    : (ASTNode**)malloc((size_t)total_n * sizeof(ASTNode*));
                gcol_i = (int64_t**)calloc(nattr, sizeof(int64_t*));
                gcol_s = (const char***)calloc(nattr, sizeof(const char**));
                gitems = gcache->items;  /* NULL si pure_columnar */
                for (int k = 0; k < nattr; k++) {
                    const char *t = cls->attributes[k].type;
                    int kind = 3;
                    if (t) {
                        if (!strcmp(t,"int") || !strcmp(t,"INT") || !strcmp(t,"long") ||
                            !strcmp(t,"Integer") || !strcmp(t,"bool") || !strcmp(t,"BOOL")) kind = 0;
                        else if (!strcmp(t,"float") || !strcmp(t,"FLOAT") || !strcmp(t,"double") ||
                                 !strcmp(t,"Double")) kind = 1;
                        else if (!strcmp(t,"string") || !strcmp(t,"STRING") || !strcmp(t,"String")) kind = 2;
                    }
                    gcache->kinds[k] = kind;
                    if (kind == 0) {
                        gcache->int_cols[k] = (int64_t*)malloc((size_t)total_n * sizeof(int64_t));
                        gcol_i[k] = gcache->int_cols[k];
                    } else if (kind == 2) {
                        gcache->str_cols[k] = (const char**)malloc((size_t)total_n * sizeof(const char*));
                        gcol_s[k] = gcache->str_cols[k];
                    }
                }
                for (int i = 0; i < N; i++) {
                    args[i].gcol_i = gcol_i;
                    args[i].gcol_s = gcol_s;
                    args[i].gitems = gitems;
                }
            } else {
                prep_ok = 0;
            }
        }

        for (int i = 0; i < N; i++) {
            pthread_create(&tids[i], NULL, csv_parse_worker, &args[i]);
        }
        for (int i = 0; i < N; i++) pthread_join(tids[i], NULL);

        /* Linkar arenas y pools de cada worker para que sobrevivan al exit. */
        for (int i = 0; i < N; i++) {
            csv_arena_keepalive_link(args[i].arena_head);
            ast_pool_keepalive_link(args[i].pool_head);
            if (args[i].first) {
                if (!first) { first = args[i].first; last = args[i].last; }
                else        { last->next = args[i].first; last = args[i].last; }
            }
        }

        /* Verificar que el pre-count coincidió con lo realmente parseado. Si
         * algún worker parseó más/menos, abortamos el direct-write y caemos
         * al pase legacy te_colcache_build (seguridad ante chunks con
         * comportamiento de borde inesperado). */
        if (prep_ok && gcache) {
            int actual_total = 0;
            int mismatch = 0;
            for (int i = 0; i < N; i++) {
                actual_total += args[i].wrow_count;
                /* Cada worker debió producir exactamente (offset_siguiente - offset_actual) filas */
                int expected = (i + 1 < N) ? (args[i+1].row_offset - args[i].row_offset)
                                           : (total_n - args[i].row_offset);
                if (args[i].wrow_count != expected) mismatch = 1;
            }
            if (mismatch || actual_total != total_n) {
                /* Limpiar el colcache direct-write y dejar que te_colcache_build
                 * haga el pase clásico. */
                for (int k = 0; k < nattr; k++) {
                    if (gcache->int_cols[k]) free(gcache->int_cols[k]);
                    if (gcache->str_cols[k]) free(gcache->str_cols[k]);
                }
                free(gcache->int_cols); free(gcache->flt_cols); free(gcache->str_cols);
                free(gcache->kinds); free(gcache->items); free(gcache);
                gcache = NULL;
            }
        }
        free(gcol_i); free(gcol_s);

        worker_args = args;
        worker_args_n = N;
        worker_gcache = gcache;
        free(tids);
    } else
#endif
    {
        /* Serial fallback: 1 chunk = todo. Path direct-write opcional. */
        const char *eco_pre = getenv("TE_COLCACHE");
        int want_colcache = (!eco_pre || eco_pre[0] != '0');
        int pure_columnar;
        if (g_te_csv_columnar_next >= 0) { pure_columnar = g_te_csv_columnar_next; g_te_csv_columnar_next = -1; }
        else pure_columnar = getenv("TE_CSV_COLUMNAR") ? 1 : 0;
        CSVWorkerArgs sa;
        memset(&sa, 0, sizeof(sa));
        sa.chunk_start = pos;
        sa.chunk_end = len;
        sa.is_first = 1;
        TeColCache *gcache = NULL;
        int64_t **gcol_i = NULL;
        const char ***gcol_s = NULL;
        if (want_colcache) {
            int total_n = (pos < len) ? (int)csv_count_newlines(src, pos, len) : 0;
            /* No '\n' final → +1 para fila terminada por EOF */
            if (total_n > 0 && src[len-1] != '\n') total_n++;
            if (total_n > 0) {
                gcache = (TeColCache*)calloc(1, sizeof(TeColCache));
                gcache->cls = cls;
                gcache->n_rows = total_n;
                gcache->nattr = nattr;
                gcache->kinds = (int*)calloc(nattr, sizeof(int));
                gcache->int_cols = (int64_t**)calloc(nattr, sizeof(int64_t*));
                gcache->flt_cols = (double**)calloc(nattr, sizeof(double*));
                gcache->str_cols = (const char***)calloc(nattr, sizeof(const char**));
                gcache->items = (ASTNode**)malloc((size_t)total_n * sizeof(ASTNode*));
                gcol_i = (int64_t**)calloc(nattr, sizeof(int64_t*));
                gcol_s = (const char***)calloc(nattr, sizeof(const char**));
                for (int k = 0; k < nattr; k++) {
                    const char *t = cls->attributes[k].type;
                    int kind = 3;
                    if (t) {
                        if (!strcmp(t,"int") || !strcmp(t,"INT") || !strcmp(t,"long") ||
                            !strcmp(t,"Integer") || !strcmp(t,"bool") || !strcmp(t,"BOOL")) kind = 0;
                        else if (!strcmp(t,"float") || !strcmp(t,"FLOAT") || !strcmp(t,"double") ||
                                 !strcmp(t,"Double")) kind = 1;
                        else if (!strcmp(t,"string") || !strcmp(t,"STRING") || !strcmp(t,"String")) kind = 2;
                    }
                    gcache->kinds[k] = kind;
                    if (kind == 0) {
                        gcache->int_cols[k] = (int64_t*)malloc((size_t)total_n * sizeof(int64_t));
                        gcol_i[k] = gcache->int_cols[k];
                    } else if (kind == 2) {
                        gcache->str_cols[k] = (const char**)malloc((size_t)total_n * sizeof(const char*));
                        gcol_s[k] = gcache->str_cols[k];
                    }
                }
                sa.gcol_i = gcol_i;
                sa.gcol_s = gcol_s;
                sa.gitems = gcache->items;  /* NULL si pure_columnar */
                sa.row_offset = 0;
            }
        }
        csv_parse_chunk(&cfg, src, len, pos, len, /*is_first=*/1, &sa);
        first = sa.first; last = sa.last;
        ast_pool_keepalive_link(t_ast_pool);
        t_ast_pool = NULL;
        if (gcache && sa.wrow_count != gcache->n_rows) {
            /* Mismatch: descartar y dejar que te_colcache_build haga el pase. */
            for (int k = 0; k < nattr; k++) {
                if (gcache->int_cols[k]) free(gcache->int_cols[k]);
                if (gcache->str_cols[k]) free(gcache->str_cols[k]);
            }
            free(gcache->int_cols); free(gcache->flt_cols); free(gcache->str_cols);
            free(gcache->kinds); free(gcache->items); free(gcache);
            gcache = NULL;
        }
        free(gcol_i); free(gcol_s);
        worker_gcache = gcache;
    }

    if (te_timing) clock_gettime(CLOCK_MONOTONIC, &ts_after_parse);

    csv_free_record(header, header_n);
    free(col_to_attr);
    free(attr_kind);
    free(attr_nullable);
    free(shared_attr_id_arena);
    free(shared_attr_type_arena);
    /* `src` NO se libera: las strings de los objetos apuntan dentro (zero-copy
     * desde mmap con MAP_PRIVATE). Vive lo que dura el proceso. */

    ASTNode* listNode = calloc(1, sizeof(ASTNode));
    listNode->type = strdup("LIST");
    listNode->left = first;
    listNode->right = NULL;
    listNode->next = NULL;
    listNode->id = NULL;
    listNode->str_value = NULL;
    listNode->value = 1; /* PREINIT FLAG: skip clone+ctor en declare_variable */
    if (te_timing) {
        clock_gettime(CLOCK_MONOTONIC, &ts_end);
        long us_mmap   = (ts_after_mmap.tv_sec   - ts0.tv_sec)*1000000L + (ts_after_mmap.tv_nsec   - ts0.tv_nsec)/1000L;
        long us_header = (ts_after_header.tv_sec - ts_after_mmap.tv_sec)*1000000L + (ts_after_header.tv_nsec - ts_after_mmap.tv_nsec)/1000L;
        long us_parse  = (ts_after_parse.tv_sec  - ts_before_parse.tv_sec)*1000000L + (ts_after_parse.tv_nsec  - ts_before_parse.tv_nsec)/1000L;
        long us_end    = (ts_end.tv_sec - ts_after_parse.tv_sec)*1000000L + (ts_end.tv_nsec - ts_after_parse.tv_nsec)/1000L;
        long us_total  = (ts_end.tv_sec - ts0.tv_sec)*1000000L + (ts_end.tv_nsec - ts0.tv_nsec)/1000L;
        fprintf(stderr, "[CSV] mmap=%ldus header=%ldus parse=%ldus tail=%ldus total=%ldus\n",
                us_mmap, us_header, us_parse, us_end, us_total);
    }
    /* v0.0.13 (perf): si el path direct-write tuvo éxito, attachamos el
     * colcache prebuilt (cero recorrido extra). Si falló por mismatch o
     * TE_COLCACHE=0, opcionalmente caemos al pase legacy te_colcache_build. */
    {
        const char *eco = getenv("TE_COLCACHE");
        if (!eco || eco[0] != '0') {
            struct timespec tcc0, tcc1;
            if (te_timing) clock_gettime(CLOCK_MONOTONIC, &tcc0);
            if (worker_gcache) {
                te_colcache_attach_prebuilt(listNode, worker_gcache);
                if (te_timing) {
                    clock_gettime(CLOCK_MONOTONIC, &tcc1);
                    long us = (tcc1.tv_sec - tcc0.tv_sec)*1000000L + (tcc1.tv_nsec - tcc0.tv_nsec)/1000L;
                    fprintf(stderr, "[CSV] colcache_attach=%ldus n=%d (direct-write)\n",
                            us, worker_gcache->n_rows);
                }
            } else {
                te_colcache_build(listNode, cls);
                if (te_timing) {
                    clock_gettime(CLOCK_MONOTONIC, &tcc1);
                    long us = (tcc1.tv_sec - tcc0.tv_sec)*1000000L + (tcc1.tv_nsec - tcc0.tv_nsec)/1000L;
                    fprintf(stderr, "[CSV] colcache_build=%ldus (legacy fallback)\n", us);
                }
            }
        }
    }
    free(worker_args);
    return listNode;
}

/* =====================================================================
 * v0.0.14: Lazy CSV-load registry + auto-detect of COLUMNAR-safe usage.
 *
 * The parser defers each `let|var X = from "f.csv", Cls;` load to here
 * via te_csv_lazy_register(). After yyparse() finishes, parse_file()
 * calls te_csv_lazy_resolve_all(root), which:
 *   - For each pending load, scans the AST for usages of var name.
 *   - If every usage is a "safe" pattern (LINQ aggregate on the list
 *     itself, with no element access / iteration / mutation), sets
 *     g_te_csv_columnar_next=1 → pure-columnar load (no wrappers).
 *   - Otherwise sets g_te_csv_columnar_next=0 → legacy load.
 *   - Invokes from_csv_to_list and patches var_decl->left.
 *
 * This eliminates the need for the TE_CSV_COLUMNAR=1 env var in the
 * common analytics case (sumBy / countWhere / .length etc.) while
 * preserving correctness for `for x in list`, `list[i]`, `.first()`,
 * etc., which still need wrappers.
 * ===================================================================== */

int g_te_csv_columnar_next = -1;

typedef struct CsvLazyEntry {
    ASTNode *var_decl;
    char *filename;
    char *class_name;
} CsvLazyEntry;

static CsvLazyEntry *g_lazy = NULL;
static int g_lazy_n = 0;
static int g_lazy_cap = 0;

void te_csv_lazy_register(ASTNode *var_decl, const char *filename, const char *class_name) {
    if (!var_decl || !filename || !class_name) return;
    if (g_lazy_n >= g_lazy_cap) {
        int nc = g_lazy_cap ? g_lazy_cap * 2 : 8;
        CsvLazyEntry *nb = (CsvLazyEntry*)realloc(g_lazy, (size_t)nc * sizeof(CsvLazyEntry));
        if (!nb) return;
        g_lazy = nb;
        g_lazy_cap = nc;
    }
    g_lazy[g_lazy_n].var_decl = var_decl;
    g_lazy[g_lazy_n].filename = strdup(filename);
    g_lazy[g_lazy_n].class_name = strdup(class_name);
    g_lazy_n++;
}

/* Safe LINQ methods on a CSV-loaded list that work in pure-columnar mode
 * (read directly from col_cache, never deref items[]). Keep in sync with
 * te_linq_ops.c columnar fast-paths. */
static int csv_lazy_method_is_safe(const char *m) {
    if (!m) return 0;
    static const char *SAFE[] = {
        "sumBy", "countWhere", "avgBy", "minBy", "maxBy", "groupBy",
        "sum", "count", "avg", "min", "max", "length",
        NULL
    };
    for (int i = 0; SAFE[i]; i++) if (strcmp(m, SAFE[i]) == 0) return 1;
    return 0;
}

/* Attributes on the list itself that are safe in columnar mode. */
static int csv_lazy_attr_is_safe(const char *a) {
    if (!a) return 0;
    return strcmp(a, "length") == 0 || strcmp(a, "count") == 0 || strcmp(a, "size") == 0;
}

/* Walk `node` (with `parent`), classify any IDENTIFIER/ID referring to
 * `var_name`. Sets *unsafe = 1 on first unsafe usage. */
static void csv_lazy_scan(ASTNode *node, ASTNode *parent, const char *var_name, int *unsafe) {
    if (!node || *unsafe) return;
    static int depth = 0;
    if (++depth > 100000) { depth--; *unsafe = 1; return; }  /* safety net */

    if (node->type && (strcmp(node->type, "IDENTIFIER") == 0 || strcmp(node->type, "ID") == 0)
        && node->id && strcmp(node->id, var_name) == 0) {
        /* This is a reference to our CSV list. Inspect parent context. */
        int safe = 0;
        if (parent && parent->type) {
            if (strcmp(parent->type, "CALL_METHOD") == 0 && parent->left == node) {
                /* receiver of a method call: e.g. list.sumBy(...) */
                if (csv_lazy_method_is_safe(parent->id)) safe = 1;
            } else if (strcmp(parent->type, "ACCESS_ATTR") == 0 && parent->left == node) {
                /* receiver of attribute access: list.length — attr name lives
                 * in parent->right (an ID leaf), not in parent->id. */
                if (parent->right && parent->right->id
                    && csv_lazy_attr_is_safe(parent->right->id)) safe = 1;
            } else if (strcmp(parent->type, "VAR_DECL") == 0 && parent->left == node) {
                /* The init link from var_decl to itself — ignore. Shouldn't
                 * happen via traversal since we placed the IDENTIFIER inside
                 * an expression, but stay defensive. */
                safe = 1;
            }
        }
        if (!safe) { *unsafe = 1; return; }
        /* When safe, do NOT recurse into the parent — but since we're
         * already inside it from the caller, just continue normally. */
    }

    /* Recurse into children. Standard ASTNode child links.
     * NOTE: `extra` is overloaded — for OBJECT/LIST/MAP it holds non-ASTNode
     * pointers (ObjectNode*, list index, map hash) and dereferencing those as
     * ASTNode would segfault. Skip extra for those types. */
    csv_lazy_scan(node->left,  node, var_name, unsafe);
    csv_lazy_scan(node->right, node, var_name, unsafe);
    csv_lazy_scan(node->next,  node, var_name, unsafe);
    if (node->extra && node->type) {
        const char *t = node->type;
        int skip_extra = (strcmp(t, "OBJECT") == 0 || strcmp(t, "LIST") == 0
                       || strcmp(t, "MAP") == 0 || strcmp(t, "THIS") == 0
                       || strcmp(t, "DATAFRAME") == 0);
        if (!skip_extra) csv_lazy_scan(node->extra, node, var_name, unsafe);
    }
    depth--;
}

void te_csv_lazy_resolve_all(ASTNode *root) {
    if (g_lazy_n == 0) return;
    int debug = getenv("TE_CSV_AUTO_DEBUG") ? 1 : 0;

    for (int i = 0; i < g_lazy_n; i++) {
        CsvLazyEntry *e = &g_lazy[i];
        ASTNode *vd = e->var_decl;
        const char *vname = (vd && vd->id) ? vd->id : NULL;
        ClassNode *cls = find_class(e->class_name);

        if (!vname || !cls) {
            if (!cls) fprintf(stderr, "Clase '%s' no encontrada.\n", e->class_name);
            free(e->filename); free(e->class_name);
            continue;
        }

        /* Scan AST for unsafe usages of `vname`. */
        int unsafe = 0;
        csv_lazy_scan(root, NULL, vname, &unsafe);

        /* Manual env override still wins if set. */
        const char *force_env = getenv("TE_CSV_COLUMNAR");
        if (force_env && force_env[0] == '1') {
            g_te_csv_columnar_next = 1;
            if (debug) fprintf(stderr, "[CSV-AUTO] %s: forced COLUMNAR via env\n", vname);
        } else if (force_env && force_env[0] == '0') {
            g_te_csv_columnar_next = 0;
            if (debug) fprintf(stderr, "[CSV-AUTO] %s: forced legacy via env\n", vname);
        } else {
            g_te_csv_columnar_next = unsafe ? 0 : 1;
            if (debug) fprintf(stderr, "[CSV-AUTO] %s: %s (unsafe_usage=%d)\n",
                vname, unsafe ? "legacy" : "COLUMNAR", unsafe);
        }

        ASTNode *list = from_csv_to_list(e->filename, cls);
        if (vd) {
            /* Free placeholder (small empty LIST node) and install real list. */
            ASTNode *placeholder = vd->left;
            vd->left = list;
            if (placeholder && placeholder != list) {
                /* Placeholder created with create_ast_node — safe to free its
                 * shallow type/id strings. Do NOT call free_ast on it (no
                 * children). */
                if (placeholder->type) free(placeholder->type);
                if (placeholder->id) free(placeholder->id);
                free(placeholder);
            }
        }

        g_te_csv_columnar_next = -1;
        free(e->filename);
        free(e->class_name);
    }
    g_lazy_n = 0;
}
