// Generalización de extract_cases.cjs: extrae bloques `case A: case B: ... {` (la última etiqueta
// termina en `{`) de un switch dentro de una función cualquiera a helpers static con la misma
// firma/return, dejando `case ...: return <helper>(<args>);`. Movimiento puro.
// Uso: node extract_cases2.cjs <file> '<json>'
//  json: { "fn": "bc_compile", "ret": "int", "params": "ASTNode *node, Instr *out, int *pos, int max",
//          "args": "node, out, pos, max", "prefix": "bc_c_",
//          "cases": [ { "label": "NK_ACCESS_ATTR", "name": "access_attr" }, ... ] }
//  "label" es la ÚLTIMA etiqueta del grupo (la que lleva la `{`).
const fs = require('fs');
const [, , file, json] = process.argv;
const cfg = JSON.parse(json);
let src = fs.readFileSync(file, 'utf8'); const crlf = src.includes('\r\n'); if (crlf) src = src.replace(/\r\n/g, '\n');
let lines = src.split('\n');
const hdr = lines.findIndex(l => new RegExp('^(static\\s+)?[A-Za-z_][\\w\\s\\*]*\\b' + cfg.fn + '\\s*\\([^;]*\\)\\s*\\{\\s*$').test(l));
if (hdr < 0) throw new Error('fn no encontrada: ' + cfg.fn);
let end = hdr + 1; while (!/^\}\s*$/.test(lines[end])) end++;
const strip = l => l.replace(/"(\\.|[^"\\])*"|'(\\.|[^'\\])*'|\/\/.*$/g, '');
const found = cfg.cases.map(it => {
  const i = lines.findIndex((l, idx) => idx > hdr && idx < end && new RegExp('(^|\\s)case ' + it.label + ':\\s*\\{\\s*$').test(l));
  if (i < 0) throw new Error('case no encontrado: ' + it.label);
  const indent = lines[i].match(/^\s*/)[0];
  let d = 0, j = i;
  for (; j <= end; j++) { for (const ch of strip(lines[j])) { if (ch === '{') d++; else if (ch === '}') d--; } if (d === 0) break; }
  if (!new RegExp('^' + indent + '\\}\\s*$').test(lines[j])) throw new Error(it.label + ': cierre inesperado: ' + lines[j]);
  const body = lines.slice(i + 1, j);
  if (body.some(l => new RegExp('^' + indent + '    break;').test(l))) throw new Error(it.label + ': break a nivel de case (caería tras el switch)');
  for (const m of body.join('\n').matchAll(/\bgoto\s+([A-Za-z_]\w*)/g)) if (!new RegExp('^\\s*' + m[1] + ':', 'm').test(body.join('\n'))) throw new Error(it.label + ': goto ' + m[1] + ' fuera del bloque');
  return { ...it, i, j, indent, labels: lines[i].replace(/\{\s*$/, '').trim() };
}).sort((a, b) => b.i - a.i);
const helpers = [];
for (const it of found) {
  const body = lines.slice(it.i + 1, it.j).map(l => l.startsWith(it.indent) ? l.slice(it.indent.length) : l);
  helpers.push([`/* ${it.labels} — extraído de ${cfg.fn} (Fase 2). */`,
    `static ${cfg.ret} ${cfg.prefix}${it.name}(${cfg.params}) {`, ...body, `}`, ``].join('\n'));
  lines.splice(it.i, it.j - it.i + 1, `${it.indent}${it.labels} return ${cfg.prefix}${it.name}(${cfg.args});`);
}
// insertar helpers justo antes de la fn (después de su prototipo, si lo hay: los helpers pueden
// llamar a la fn recursiva)
lines.splice(hdr, 0, ...helpers.reverse().join('\n').split('\n'));
let out = lines.join('\n'); if (crlf) out = out.replace(/\n/g, '\r\n');
fs.writeFileSync(file, out);
console.log(`${cfg.fn}: ${found.length} cases -> ${found.map(f => cfg.prefix + f.name).reverse().join(', ')}`);
