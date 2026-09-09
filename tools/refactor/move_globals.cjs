// move_globals.cjs — Fase B2 (0.1.1): mover globales de proceso a estado por-VM.
// Uso: node tools/refactor/move_globals.cjs <map.json> [file ...]
//   map.json: { "g_symbol": "replacement_expr", ... }
//   Renombra tokens FUERA de strings y comentarios en los archivos dados (default: src/*.{c,h,y,l}
//   salvo generados), BORRA las líneas de definición a nivel de archivo y los `extern` de esos
//   símbolos. Las definiciones borradas se listan en stdout con su inicializador para que el
//   llamador las traslade al struct destino (los valores no-cero se ponen en el constructor lazy).
const fs = require('fs');
const [, , mapPath, ...argFiles] = process.argv;
if (!mapPath) { console.error('uso: move_globals.cjs <map.json> [files]'); process.exit(2); }
const map = JSON.parse(fs.readFileSync(mapPath, 'utf8'));
const names = Object.keys(map).sort((a, b) => b.length - a.length);
const esc = s => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
const alt = names.map(esc).join('|');
const tokRe = new RegExp('(?<![\\w.>])(' + alt + ')\\b', 'g');
const files = argFiles.length ? argFiles
  : fs.readdirSync('src').filter(f => /\.(c|h|y|l)$/.test(f) && !/^(parser\.tab|lex\.yy)\./.test(f)).map(f => 'src/' + f);
const externRe = new RegExp('^\\s*extern\\s+[^;(]*\\b(' + alt + ')\\b\\s*(\\[[^\\]]*\\])*\\s*;\\s*(//.*|/\\*.*\\*/)?\\s*$');
// definición a nivel de archivo (col 0): [static] [const] [volatile] [__thread] tipo [*] nombre [dims] [= init];
const defRe = new RegExp('^(static\\s+)?(const\\s+)?(volatile\\s+)?(__thread\\s+)?[A-Za-z_][\\w\\s\\*]*[\\s\\*](' + alt + ')\\s*((\\[[^\\]]*\\])*)\\s*(=\\s*([^;]+))?;\\s*(//.*|/\\*.*\\*/)?\\s*$');
function renameCode(line) {
  let out = '', n = 0;
  const seg = /("(\\.|[^"\\])*"|'(\\.|[^'\\])*'|\/\/.*$|\/\*[\s\S]*?\*\/|\/\*.*$)/g;
  let m, last = 0;
  while ((m = seg.exec(line))) {
    out += line.slice(last, m.index).replace(tokRe, (_, t) => { n++; return map[t]; });
    out += m[0]; last = m.index + m[0].length;
  }
  out += line.slice(last).replace(tokRe, (_, t) => { n++; return map[t]; });
  return [out, n];
}
let total = 0; const deleted = [];
for (const f of files) {
  if (!fs.existsSync(f)) continue;
  const orig = fs.readFileSync(f, 'utf8'); const eol = orig.includes('\r\n') ? '\r\n' : '\n';
  let lines = orig.split(/\r?\n/); let changed = false, inBlock = false;
  lines = lines.flatMap((l, i) => {
    if (inBlock) { if (l.includes('*/')) inBlock = false; return [l]; }
    if (/^\s*\/\*/.test(l) && !l.includes('*/')) { inBlock = true; return [l]; }
    if (/^\s*(\/\/|\*|#include)/.test(l)) return [l];
    if (externRe.test(l)) { changed = true; deleted.push(`${f}:${i + 1}: EXTERN ${l.trim()}`); return []; }
    const d = defRe.exec(l);
    if (d) { changed = true; deleted.push(`${f}:${i + 1}: DEF ${l.trim()}`); return []; }
    const [o, n] = renameCode(l);
    if (n) { changed = true; total += n; }
    return [o];
  });
  if (changed) fs.writeFileSync(f, lines.join(eol));
}
for (const d of deleted) console.log(d);
console.log('renames:', total, 'deleted:', deleted.length);
