// Fase 3C (0.1.x): mover a TeVM los registros por-VM que quedaban globales: clases
// (classes/classes_cap/class_count), endpoints (global_methods) y el valor de retorno
// (__ret_var/__ret_var_active). Renombra tokens FUERA de strings y comentarios; borra
// definiciones (ast.c) y externs. Los tests de regresión del debugger cubren el bug previo
// de renombrar dentro de literales.
const fs = require('fs');
const map = { classes: 'classes', classes_cap: 'classes_cap', class_count: 'class_count', global_methods: 'global_methods', __ret_var: 'ret_var', __ret_var_active: 'ret_var_active' };
const names = Object.keys(map).sort((a, b) => b.length - a.length);
const tokRe = new RegExp('(?<![\\w.>])(' + names.join('|') + ')\\b', 'g');
const files = fs.readdirSync('src').filter(f => /\.(c|h|y|l)$/.test(f) && !/^(parser\.tab|lex\.yy)\./.test(f) && f !== 'te_vm.h').map(f => 'src/' + f);
const externRe = new RegExp('^\\s*extern\\s+[^;(]*\\b(' + names.join('|') + ')\\b\\s*(\\[[^\\]]*\\])?\\s*;\\s*(//.*)?$');
const defRe = new RegExp('^(static\\s+)?[A-Za-z_][\\w\\s\\*]*[\\s\\*](' + names.join('|') + ')\\s*(\\[[^\\]]*\\])?\\s*(=\\s*[^;]+)?;');
// renombra solo en segmentos de código: separa strings "..." / '...' y comentarios // y /* */ (por línea)
function renameCode(line) {
  let out = '', i = 0, inBlock = false, n = 0;
  const seg = /("(\\.|[^"\\])*"|'(\\.|[^'\\])*'|\/\/.*$|\/\*[\s\S]*?\*\/|\/\*.*$)/g;
  let m, last = 0;
  while ((m = seg.exec(line))) {
    const code = line.slice(last, m.index);
    out += code.replace(tokRe, (_, t) => { n++; return 'g_vm.' + map[t]; });
    out += m[0]; last = m.index + m[0].length;
  }
  out += line.slice(last).replace(tokRe, (_, t) => { n++; return 'g_vm.' + map[t]; });
  return [out, n];
}
let total = 0;
for (const f of files) {
  const orig = fs.readFileSync(f, 'utf8'); const eol = orig.includes('\r\n') ? '\r\n' : '\n';
  let lines = orig.split(/\r?\n/); let changed = false, used = false, inBlock = false;
  lines = lines.flatMap(l => {
    // seguimiento burdo de comentarios de bloque multilínea
    if (inBlock) { if (l.includes('*/')) inBlock = false; return [l]; }
    if (/^\s*\/\*/.test(l) && !l.includes('*/')) { inBlock = true; return [l]; }
    if (/^\s*(\/\/|\*|#include)/.test(l)) return [l];
    if (externRe.test(l)) { changed = true; used = true; return []; }
    if (f === 'src/ast.c' && defRe.test(l)) { changed = true; return []; }
    const [o, n] = renameCode(l);
    if (n) { changed = true; used = true; total += n; }
    return [o];
  });
  if (used && !lines.some(l => /#include "te_vm\.h"/.test(l))) {
    let idx = lines.findIndex(l => /^#include "ast\.h"/.test(l));
    if (idx < 0 && f.endsWith('.y')) idx = lines.findIndex(l => /^\s*#include/.test(l));
    if (idx < 0) idx = lines.map((l, i) => /^\s*#include/.test(l) ? i : -1).filter(i => i >= 0).pop() ?? -1;
    if (idx >= 0) { lines.splice(idx + 1, 0, '#include "te_vm.h"'); changed = true; } else console.log('SIN INCLUDE:', f);
  }
  if (changed) fs.writeFileSync(f, lines.join(eol));
}
// te_vm.h: campos
let h = fs.readFileSync('src/te_vm.h', 'utf8');
h = h.replace('    struct TeFrame *frame_top;             /* frames de fn activos */\n} TeVM;',
`    struct TeFrame *frame_top;             /* frames de fn activos */
    /* --- Fase 3C: registros del programa que antes eran globales de proceso --- */
    ClassNode **classes; int classes_cap; int class_count;   /* clases declaradas (parser + runtime) */
    MethodNode *global_methods;                              /* endpoints/métodos globales (lista enlazada) */
    Variable ret_var; int ret_var_active;                    /* valor de retorno (__ret_var) */
} TeVM;`);
if (!/ret_var_active/.test(h)) throw new Error('te_vm.h no actualizado');
fs.writeFileSync('src/te_vm.h', h);
console.log('renames:', total);
