// Extrae `case NK_X: { ... }` (o `case NK_X:` sin llaves, hasta el `break;` final de nivel 1)
// de una función void con switch a helpers `static void te_stmt_<name>(ASTNode *node)`.
// Quita el `break;` final; otros `break;` de nivel switch quedan y el COMPILADOR los marca
// ("break statement not within loop or switch") -> se cambian a return; a mano.
// Uso: node extract_stmt_cases.cjs src/ast.c interpret_ast 'NK_METHOD_CALL_ALONE:method_call_alone,...'
const fs = require('fs');
const [, , file, fn, spec] = process.argv;
let src = fs.readFileSync(file, 'utf8'); const crlf = src.includes('\r\n'); if (crlf) src = src.replace(/\r\n/g, '\n');
let lines = src.split('\n');
const hdr = lines.findIndex(l => new RegExp('^void ' + fn + '\\(ASTNode \\*node\\) \\{\\s*$').test(l));
if (hdr < 0) throw new Error('fn');
let end = hdr; while (!/^\}\s*$/.test(lines[end])) end++;
const depthDelta = l => { let d = 0; for (const ch of l.replace(/"(\\.|[^"\\])*"|'(\\.|[^'\\])*'|\/\/.*$/g, '')) { if (ch === '{') d++; else if (ch === '}') d--; } return d; };
const items = spec.split(',').map(s => { const [k, n] = s.split(':'); return { kind: k, name: n }; });
const found = items.map(it => {
  const i = lines.findIndex((l, idx) => idx > hdr && idx < end && new RegExp('^    case ' + it.kind + ':').test(l));
  if (i < 0) throw new Error('case: ' + it.kind);
  const braced = /\{\s*$/.test(lines[i]);
  let j;
  if (braced) { let d = 0; for (j = i; j <= end; j++) { d += depthDelta(lines[j]); if (d === 0 && j > i) break; } if (!/^    \}\s*$/.test(lines[j])) throw new Error(it.kind + ': cierre'); }
  else { let d = 0; for (j = i + 1; j <= end; j++) { if (d === 0 && /^        break;\s*$/.test(lines[j])) break; d += depthDelta(lines[j]); if (/^    (case|default)\b/.test(lines[j])) throw new Error(it.kind + ': sin break final'); } }
  return { ...it, i, j, braced };
}).sort((a, b) => b.i - a.i);
const helpers = [];
for (const it of found) {
  let body = it.braced ? lines.slice(it.i + 1, it.j) : lines.slice(it.i + 1, it.j); // sin case; braced: sin '}' final; braceless: sin 'break;' final
  // quitar el break; final (si el ultimo statement no vacio es 'break;')
  let last = body.length - 1; while (last >= 0 && body[last].trim() === '') last--;
  if (last >= 0 && /^\s*break;\s*$/.test(body[last])) body = body.slice(0, last);
  helpers.push([`/* ${it.kind} — extraído de ${fn} (Fase 2). */`, `static void te_stmt_${it.name}(ASTNode *node) {`, ...body, `}`, ``].join('\n'));
  lines.splice(it.i, it.j - it.i + 1, `    case ${it.kind}: te_stmt_${it.name}(node); break;`);
}
lines.splice(hdr, 0, ...helpers.reverse().join('\n').split('\n'));
let out = lines.join('\n'); if (crlf) out = out.replace(/\n/g, '\r\n');
fs.writeFileSync(file, out);
const s = lines.findIndex(l => new RegExp('^void ' + fn + '\\(ASTNode \\*node\\) \\{').test(l)); let k = s; while (!/^\}\s*$/.test(lines[k])) k++;
console.log(fn + ':', k - s, 'lineas; helpers:', found.map(f => f.name).join(', '));
