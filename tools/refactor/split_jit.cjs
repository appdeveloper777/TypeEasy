// Fase 2 (0.1.x): partir jit_compile_trace (te_bytecode.c) en 3 helpers: validación de ops,
// pasada de hoisting y emisión del cuerpo. Copy-in/copy-out de `p` (cursor de emisión) y
// n_patches; `goto fail` dentro de los helpers -> return 0 (el caller hace goto fail).
// struct LoopVar y la macro LREG_OF pasan a ámbito de archivo (los helpers las necesitan).
const fs = require('fs');
const f = 'src/te_bytecode.c';
let src = fs.readFileSync(f, 'utf8'); const crlf = src.includes('\r\n'); if (crlf) src = src.replace(/\r\n/g, '\n');
let L = src.split('\n');
const hdr = L.findIndex(l => /^static void \*jit_compile_trace\(Trace \*t\) \{$/.test(l)); if (hdr < 0) throw new Error('hdr');
const at = o => hdr + o;
const chk = (i, re, w) => { if (!re.test(L[i])) throw new Error(`${w}: '${L[i]}'`); };
chk(at(12), /^\s{4}\/\* Validación de ops permitidas\. \*\/$/, 'valid comment');
chk(at(13), /^\s{4}for \(int i = 1; i < last_idx; i\+\+\) \{$/, 'valid for');
chk(at(75), /^\s{4}\}$/, 'valid end');
chk(at(91), /^\s{4}struct LoopVar \{ Variable \*var; int lreg; \} lvars\[TE_JIT_MAX_LREGS\];$/, 'LoopVar');
chk(at(109), /^\s{4}#define LREG_OF\(V\)/, 'LREG_OF'); chk(at(111), /^\s{8}_r; \}\)$/, 'LREG_OF end');
chk(at(135), /^\s{4}for \(int hoist_pass = /, 'hoist for'); chk(at(214), /^\s{4}\}$/, 'hoist end');
chk(at(218), /^\s{4}size_t loop_top_off = /, 'loop_top');
chk(at(220), /^\s{4}for \(int i = 1; i < t->len; i\+\+\) \{$/, 'emit for'); chk(at(357), /^\s{4}\}$/, 'emit end');
const dedent = l => l.replace(/^ {4}/, '');
const gotoToRet = l => l.replace(/\bgoto fail;/g, 'return 0;');
// --- helpers ---
const valid = L.slice(at(13), at(76)).map(dedent).map(l => l.replace(/\breturn NULL;/g, 'return 0;'));
const hoist = L.slice(at(135), at(215)).map(dedent).map(gotoToRet);
const emit = L.slice(at(220), at(358)).map(dedent).map(gotoToRet);
const lregMacro = L.slice(at(108), at(112)).map(dedent); // comentario + 3 líneas de macro
const helpers = [
  '/* Variable cacheada en registro callee-saved durante un loop trace (jit_compile_trace). */',
  'struct LoopVar { Variable *var; int lreg; };',
  ...lregMacro, '',
  '/* Pasada 1 de jit_compile_trace (extraída, Fase 2): ¿todas las ops del trace son compilables? */',
  'static int jit_trace_ops_supported(Trace *t, int last_idx) {',
  ...valid, '    return 1;', '}', '',
  '/* Hoisting de invariantes de loop (extraído de jit_compile_trace, Fase 2). 0 = fallo (caller: goto fail). */',
  'static int jit_emit_hoist(Trace *t, int is_loop, uint8_t **pp) {',
  '    uint8_t *p = *pp;',
  ...hoist, '    *pp = p; return 1;', '}', '',
  '/* Emisión del cuerpo del trace (extraída de jit_compile_trace, Fase 2). 0 = fallo (caller: goto fail). */',
  'static int jit_emit_body(Trace *t, int is_loop, uint8_t *base, uint8_t **pp, struct LoopVar *lvars, int n_lvars,',
  '                         size_t *guard_patches, int *p_n_patches, size_t loop_top_off) {',
  '    uint8_t *p = *pp; int n_patches = *p_n_patches;',
  ...emit, '    *pp = p; *p_n_patches = n_patches; return 1;', '}', ''];
// --- reemplazos en la fn (de abajo hacia arriba) ---
L.splice(at(220), 358 - 220, '    if (!jit_emit_body(t, is_loop, base, &p, lvars, n_lvars, guard_patches, &n_patches, loop_top_off)) goto fail;');
L.splice(at(135), 215 - 135, '    if (!jit_emit_hoist(t, is_loop, &p)) goto fail;');
L.splice(at(108), 112 - 108); // macro -> archivo
L[at(91)] = '    struct LoopVar lvars[TE_JIT_MAX_LREGS];';
L.splice(at(12), 76 - 12, '    if (!jit_trace_ops_supported(t, last_idx)) return NULL;');
L.splice(hdr, 0, ...helpers);
let out = L.join('\n'); if (crlf) out = out.replace(/\n/g, '\r\n');
fs.writeFileSync(f, out);
console.log('ok jit: jit_trace_ops_supported, jit_emit_hoist, jit_emit_body');
