// Fase 3 (completar paso A): mover los globales de estado por-VM restantes a TeVM.
// g_X -> g_vm.X (campo sin el prefijo g_). Definiciones y externs se eliminan.
const fs = require('fs');
const path = require('path');
const map = { g_runtime_recovery: 'runtime_recovery', g_call_depth: 'call_depth', g_max_call_depth: 'max_call_depth',
  g_current_exec_line: 'current_exec_line', g_runtime_error_line: 'runtime_error_line', g_runtime_error_msg: 'runtime_error_msg',
  g_current_exec_file: 'current_exec_file', g_runtime_error_file: 'runtime_error_file', g_callstack: 'callstack', g_callstack_n: 'callstack_n',
  g_stdout_buffer: 'stdout_buffer', g_stdout_size: 'stdout_size', g_suppress_stdout: 'suppress_stdout', g_initial_var_count: 'initial_var_count',
  g_sym_slots: 'sym_slots', g_sym_init: 'sym_init', g_frame_top: 'frame_top' };
const names = Object.keys(map);
const files = fs.readdirSync('src').filter(f => /\.(c|h|l|y)$/.test(f) && !/^(parser\.tab|lex\.yy)/.test(f)).map(f => 'src/' + f);
const externRe = new RegExp('^\\s*extern\\s+[^;]*\\b(' + names.join('|') + ')\\b[^;]*;\\s*$');
const defRe = new RegExp('^(static\\s+)?(const\\s+)?[A-Za-z_][\\w\\s\\*]*[\\s\\*](' + names.join('|') + ')\\s*(\\[[^\\]]*\\])?\\s*(=\\s*[^;]+)?;');
let renamed = 0;
for (const f of files) {
  const orig = fs.readFileSync(f, 'utf8'); const eol = orig.includes('\r\n') ? '\r\n' : '\n';
  let lines = orig.split(/\r?\n/); let changed = false, needsInclude = false;
  lines = lines.flatMap(l => {
    if (externRe.test(l)) { changed = true; needsInclude = true; return []; }
    if (f.endsWith('ast.c') && defRe.test(l)) { changed = true; return [`/* ${l.trim()}  -> g_vm (te_vm.h, Fase 3) */`]; }
    if (/^\s*(\/\/|\*|\/\*|#define|#include)/.test(l)) return [l];
    let out = l;
    for (const n of names) out = out.replace(new RegExp('(?<![\\w.>])' + n + '\\b', 'g'), () => { renamed++; needsInclude = true; return 'g_vm.' + map[n]; });
    if (out !== l) changed = true;
    return [out];
  });
  if (needsInclude && !/te_vm\.h|ast_internal\.h/.test(path.basename(f)) && !lines.some(l => /#include "te_vm\.h"/.test(l))) {
    let idx = lines.findIndex(l => /^#include "ast\.h"/.test(l));
    if (idx < 0) idx = lines.map((l, i) => /^\s*#include/.test(l) ? i : -1).filter(i => i >= 0).pop() ?? -1;
    if (idx >= 0) { lines.splice(idx + 1, 0, '#include "te_vm.h"'); changed = true; }
    else console.log('SIN INCLUDE (agregar a mano):', f);
  }
  if (changed) fs.writeFileSync(f, lines.join(eol));
}
// te_vm.h: nuevos campos + tipos necesarios
let h = fs.readFileSync('src/te_vm.h', 'utf8');
h = h.replace('#include "ast.h"', `#include <setjmp.h>
#include <stdint.h>
#include "ast.h"

#define TE_CALLSTACK_MAX 128
#define TE_SYM_CAP 16384  /* MAX_VARS=4096 -> cap 16384 mantiene load < 0.25 */
/* Slot del índice nombre->slot de vars[] (FNV-1a). key es alias del id interned. */
typedef struct TESymSlot { uint64_t hash; const char *key; int idx; } TESymSlot;
struct TeFrame;`);
h = h.replace('    ASTNode *return_node;      /* valor del return en vuelo */\n} TeVM;', `    ASTNode *return_node;      /* valor del return en vuelo */
    /* --- Fase 3 (completar A): resto del estado por VM --- */
    jmp_buf *runtime_recovery;             /* punto de recuperación de fatales (--api) */
    int call_depth, max_call_depth;
    int current_exec_line, current_exec_file;
    int runtime_error_line, runtime_error_file;
    char runtime_error_msg[256];
    const char *callstack[TE_CALLSTACK_MAX];
    int callstack_n;
    char *stdout_buffer; size_t stdout_size; int suppress_stdout;   /* captura de stdout (API) */
    int initial_var_count;                 /* globales del script; lo que sigue es por request */
    TESymSlot sym_slots[TE_SYM_CAP]; int sym_init;
    struct TeFrame *frame_top;             /* frames de fn activos */
} TeVM;`);
fs.writeFileSync('src/te_vm.h', h);
// ast.c: quitar definiciones de TE_CALLSTACK_MAX / TE_SYM_CAP / typedef TESymSlot (ahora en te_vm.h)
let a = fs.readFileSync('src/ast.c', 'utf8'); const eolA = a.includes('\r\n') ? '\r\n' : '\n';
a = a.replace(/^#define TE_CALLSTACK_MAX 128\r?\n/m, '').replace(/^#define TE_SYM_CAP 16384[^\n]*\r?\n/m, '')
     .replace(/typedef struct TESymSlot \{\r?\n\s*uint64_t hash;\r?\n\s*const char \*key;[^\n]*\r?\n\s*int idx;\r?\n\} TESymSlot;\r?\n/, '/* TESymSlot: ahora en te_vm.h */' + eolA);
fs.writeFileSync('src/ast.c', a);
console.log('renames:', renamed, '| campos:', names.length);
