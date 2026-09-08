// Extrae bloques `case NK_X: { ... }` de una función que devuelve `double` a helpers
// `static double te_ev_<name>(ASTNode *node)`; el case queda `case NK_X: return te_ev_<name>(node);`.
// Si algún camino del bloque no retornaba, el compilador lo marca (-Wreturn-type).
// Uso: node extract_cases.cjs src/ast.c evaluate_expression 'NK_EQ:eq,NK_DIFF:diff,...'
const fs = require('fs');
const [, , file, fn, spec] = process.argv;
let src = fs.readFileSync(file, 'utf8'); const crlf = src.includes('\r\n'); if (crlf) src = src.replace(/\r\n/g, '\n');
let lines = src.split('\n');
const hdr = lines.findIndex(l => new RegExp('^double ' + fn + '\\(ASTNode \\*node\\) \\{\\s*$').test(l));
if (hdr < 0) throw new Error('fn');
let end = hdr; while (!/^\}\s*$/.test(lines[end])) end++;
const items = spec.split(',').map(s => { const [k, n] = s.split(':'); return { kind: k, name: n }; });
const helpers = [];
// procesar de abajo hacia arriba
const found = items.map(it => {
  const i = lines.findIndex((l, idx) => idx > hdr && idx < end && new RegExp('^    case ' + it.kind + ':\\s*\\{\\s*$').test(l));
  if (i < 0) throw new Error('case no encontrado: ' + it.kind);
  let d = 0, j = i;
  for (; j <= end; j++) { for (const ch of lines[j].replace(/"(\\.|[^"\\])*"|'(\\.|[^'\\])*'|\/\/.*$/g, '')) { if (ch === '{') d++; else if (ch === '}') d--; } if (d === 0 && j > i) break; }
  if (!/^    \}\s*$/.test(lines[j])) throw new Error(it.kind + ': cierre inesperado ' + lines[j]);
  return { ...it, i, j };
}).sort((a, b) => b.i - a.i);
for (const it of found) {
  const body = lines.slice(it.i + 1, it.j); // sin la línea case ni la llave de cierre
  helpers.push([`/* ${it.kind} — extraído de ${fn} (Fase 2). */`, `static double te_ev_${it.name}(ASTNode *node) {`, ...body, `}`, ``].join('\n'));
  lines.splice(it.i, it.j - it.i + 1, `    case ${it.kind}: return te_ev_${it.name}(node);`);
}
lines.splice(hdr, 0, ...helpers.reverse().join('\n').split('\n'));
let out = lines.join('\n'); if (crlf) out = out.replace(/\n/g, '\r\n');
fs.writeFileSync(file, out);
const s = lines.findIndex(l => new RegExp('^double ' + fn + '\\(ASTNode \\*node\\) \\{').test(l)); let k = s; while (!/^\}\s*$/.test(lines[k])) k++;
console.log(fn + ':', k - s, 'lineas; helpers:', found.map(f => f.name).join(', '));
