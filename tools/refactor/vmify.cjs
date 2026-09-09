// vmify.cjs — mueve globales `g_*` de un módulo a campos de una struct (TeVM o un
// sub-struct) renombrando tokens FUERA de strings/comentarios y borrando la
// definición a nivel de archivo + externs en el resto del árbol.
//
//   node tools/refactor/vmify.cjs spec.json
//   spec = { "files": ["src/debugger.c"],          // donde están las DEFINICIONES
//            "prefix": "g_vm.dbg->",                // expresión que reemplaza al nombre
//            "map": { "g_client_fd": "client_fd", ... },
//            "keepDef": ["g_rx"] }                  // opcional: no borrar la definición
// Los externs de esos nombres se borran en TODO src/ (y tests/fuzz). Cualquier otro
// archivo que use el nombre también se renombra (e incluye te_vm.h si hace falta).
const fs = require('fs');
const spec = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const map = spec.map; const prefix = spec.prefix;
const keepDef = new Set(spec.keepDef || []);
const names = Object.keys(map).sort((a, b) => b.length - a.length);
const alt = names.join('|');
const tokRe = new RegExp('(?<![\\w.>])(' + alt + ')\\b', 'g');
const externRe = new RegExp('^\\s*extern\\s+[^;(]*\\b(' + alt + ')\\b\\s*(\\[[^\\]]*\\])*\\s*;\\s*(/[/*].*)?$');
// definición a nivel de archivo (columna 0), con o sin static/volatile/const, arrays e inicializador
const defRe = new RegExp('^(static\\s+)?(volatile\\s+)?(const\\s+)?[A-Za-z_][\\w\\s\\*]*[\\s\\*](' + alt + ')\\s*(\\[[^\\]]*\\])*\\s*(=|;)');
const srcFiles = fs.readdirSync('src').filter(f => /\.(c|h|y|l)$/.test(f) && !/^(parser\.tab|lex\.yy)\./.test(f) && f !== 'te_vm.h').map(f => 'src/' + f);
const files = srcFiles.concat(fs.existsSync('tests/fuzz/fuzz_parser.c') ? ['tests/fuzz/fuzz_parser.c'] : []);
const defFiles = new Set(spec.files);

function renameCode(line) {
  let out = '', n = 0;
  const seg = /("(\\.|[^"\\])*"|'(\\.|[^'\\])*'|\/\/.*$|\/\*[\s\S]*?\*\/|\/\*.*$)/g;
  let m, last = 0;
  while ((m = seg.exec(line))) {
    out += line.slice(last, m.index).replace(tokRe, (_, t) => { n++; return prefix + map[t]; });
    out += m[0]; last = m.index + m[0].length;
  }
  out += line.slice(last).replace(tokRe, (_, t) => { n++; return prefix + map[t]; });
  return [out, n];
}
let total = 0, deleted = [];
for (const f of files) {
  const orig = fs.readFileSync(f, 'utf8'); const eol = orig.includes('\r\n') ? '\r\n' : '\n';
  let lines = orig.split(/\r?\n/); let changed = false, used = false, inBlock = false, skipInit = false;
  const out = [];
  for (let l of lines) {
    if (skipInit) { // continuación de un inicializador multilínea borrado
      if (/;\s*$/.test(l)) skipInit = false;
      changed = true; continue;
    }
    if (inBlock) { if (l.includes('*/')) inBlock = false; out.push(l); continue; }
    if (/^\s*\/\*/.test(l) && !l.includes('*/')) { inBlock = true; out.push(l); continue; }
    if (/^\s*(\/\/|\*|#include)/.test(l)) { out.push(l); continue; }
    if (externRe.test(l)) { changed = true; used = true; continue; }
    if (defFiles.has(f)) {
      const dm = defRe.exec(l);
      if (dm && !keepDef.has(dm[4])) {
        deleted.push(dm[4]); changed = true;
        if (!/;\s*(\/[/*].*)?$/.test(l)) skipInit = true;
        continue;
      }
    }
    const [o, n] = renameCode(l);
    if (n) { changed = true; used = true; total += n; }
    out.push(o);
  }
  lines = out;
  if (used && /^g_vm\./.test(prefix) && !lines.some(l => /#include "te_vm\.h"/.test(l))) {
    let idx = lines.findIndex(l => /^#include "ast\.h"/.test(l));
    if (idx < 0) idx = lines.map((l, i) => /^\s*#include/.test(l) ? i : -1).filter(i => i >= 0).pop() ?? -1;
    if (idx >= 0) { lines.splice(idx + 1, 0, '#include "te_vm.h"'); changed = true; } else console.log('SIN INCLUDE:', f);
  }
  if (changed) fs.writeFileSync(f, lines.join(eol));
}
const missing = names.filter(n => !deleted.includes(n) && !keepDef.has(n));
console.log('renames:', total, 'defs borradas:', deleted.length, missing.length ? 'SIN DEF BORRADA: ' + missing.join(' ') : '');
