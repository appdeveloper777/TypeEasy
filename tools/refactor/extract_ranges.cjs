// Extract-range genérico: corta rangos de líneas de una función (bloques `if (...) {...}` de
// nivel 1 que terminan en `return 1;` o caen) a helpers `static int NAME(PARAMS)` que
// devuelven 1 si manejaron y 0 si no. Cuerpos idénticos (return 1 se conserva; `return 0;`
// en el cuerpo sería un error: se verifica). Config por JSON en argv[2].
// Uso: node extract_ranges.cjs <file.c> '<json>'
//   json: { "fn": "te_builtin_dispatch", "ret": "int",
//           "blocks": [ { "name": "te_bi_core", "from": 13, "to": 231,
//                         "params": "const char *fn, ASTNode *node, ASTNode *a0, ASTNode *a1",
//                         "args": "fn, node, a0, a1" } ] }
// from/to son offsets relativos a la línea de cabecera de la función (1 = primera línea del cuerpo).
const fs = require('fs');
const [, , file, json] = process.argv;
const cfg = JSON.parse(json);
let src = fs.readFileSync(file, 'utf8');
const crlf = src.includes('\r\n'); if (crlf) src = src.replace(/\r\n/g, '\n');
let lines = src.split('\n');
const hdr = lines.findIndex(l => new RegExp('^[A-Za-z_][^;]*\\b' + cfg.fn + '\\s*\\([^;]*\\)\\s*\\{\\s*$').test(l));
if (hdr < 0) throw new Error('fn no encontrada: ' + cfg.fn);
const helpers = [];
const blocks = [...cfg.blocks].sort((a, b) => b.from - a.from);
for (const b of blocks) {
  const i = hdr + b.from, j = hdr + b.to;
  if (!/^\s*\}\s*$/.test(lines[j]) && !b.looseEnd) throw new Error(`${b.name}: la línea final (${b.to}) no es '}': ${lines[j]}`);
  const body = lines.slice(i, j + 1);
  const text = body.join('\n');
  // balance de llaves del rango
  let d = 0; for (const ch of text.replace(/\/\*[\s\S]*?\*\//g, "").replace(/"(\\.|[^"\\])*"|'(\\.|[^'\\])*'|\/\/.*$/gm, "")) { if (ch === '{') d++; else if (ch === '}') d--; }
  if (d !== 0) throw new Error(`${b.name}: llaves desbalanceadas (${d})`);
  if (/^\s*return 0;\s*$/m.test(text) && cfg.ret === 'int') throw new Error(`${b.name}: contiene 'return 0;' (ambiguo)`);
  // goto solo si su etiqueta está dentro del rango
  for (const m of text.matchAll(/\bgoto\s+([A-Za-z_]\w*)/g)) if (!new RegExp('^' + m[1] + ':', 'm').test(text)) throw new Error(`${b.name}: goto ${m[1]} fuera del rango`);
  // función void: `return;` dentro del bloque = "manejado" -> return 1
  const bodyOut = cfg.ret === 'void' ? body.map(l => l.replace(/\breturn;/g, 'return 1;')) : body;
  helpers.push([`/* Extraído de ${cfg.fn} (Fase 2). Devuelve 1 si manejó la llamada. */`,
    `static int ${b.name}(${b.params}) {`, ...bodyOut, `    return 0;`, `}`, ``].join('\n'));
  lines.splice(i, j - i + 1, cfg.ret === 'void' ? `    if (${b.name}(${b.args})) return;` : `    if (${b.name}(${b.args})) return 1;`);
}
lines.splice(hdr, 0, ...helpers.reverse().join('\n').split('\n'));
let out = lines.join('\n'); if (crlf) out = out.replace(/\n/g, '\r\n');
fs.writeFileSync(file, out);
const s = lines.findIndex(l => new RegExp('^[A-Za-z_][^;]*\\b' + cfg.fn + '\\s*\\([^;]*\\)\\s*\\{\\s*$').test(l));
let k = s; while (!/^\}\s*$/.test(lines[k])) k++;
console.log(`${cfg.fn}: ${k - s} lineas; helpers: ${cfg.blocks.map(b => b.name).join(', ')}`);
