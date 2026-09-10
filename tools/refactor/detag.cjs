// detag.cjs — reemplaza literales de TAG DE TIPO ("STRING", "INT", "int", ...) por las
// constantes de src/te_types.h, FUERA de comentarios. Semánticamente neutro: cada
// constante expande al mismo literal. Solo toca literales que son EXACTAMENTE un tag
// (`"STRING"`), nunca texto que lo contenga (`"expected STRING"`).
//
//   node tools/refactor/detag.cjs [--check] [archivos...]
//   sin archivos: src/*.{c,h,y} (menos generados, parser.l y te_types.h) + api_server/*.c
//   --check: no escribe; imprime cuántos literales quedan (exit 1 si > 0).
const fs = require('fs');
const path = require('path');

const hdr = fs.readFileSync(path.join('src', 'te_types.h'), 'utf8');
const map = {};                                   // "STRING" -> TE_T_STRING
for (const m of hdr.matchAll(/^#define\s+(TE_(?:T|DT|SYM)_\w+)\s+("[^"]+")/gm)) map[m[2]] = m[1];

const args = process.argv.slice(2);
const check = args.includes('--check');
let files = args.filter(a => !a.startsWith('--'));
if (!files.length) {
  files = fs.readdirSync('src')
    .filter(f => /\.(c|h|y)$/.test(f) && !/^(parser\.tab|lex\.yy)\./.test(f) && f !== 'te_types.h')
    .map(f => 'src/' + f);
  if (fs.existsSync('api_server'))
    files = files.concat(fs.readdirSync('api_server').filter(f => /\.(c|h)$/.test(f)).map(f => 'api_server/' + f));
}

// segmentos: string, char, comentario de línea, comentario de bloque (una línea o abierto)
const seg = /("(\\.|[^"\\])*"|'(\\.|[^'\\])*'|\/\/.*$|\/\*[\s\S]*?\*\/|\/\*.*$)/g;
let total = 0, remaining = 0;
const perFile = [];
for (const f of files) {
  const orig = fs.readFileSync(f, 'utf8');
  const eol = orig.includes('\r\n') ? '\r\n' : '\n';
  const lines = orig.split(/\r?\n/);
  let n = 0, inBlock = false;
  const out = lines.map(line => {
    if (inBlock) { if (line.includes('*/')) { inBlock = false; } return line; }
    if (/^\s*#\s*define\b/.test(line)) return line;   // definiciones de constantes: el único sitio legítimo del literal
    let res = '', last = 0, m;
    seg.lastIndex = 0;
    while ((m = seg.exec(line))) {
      res += line.slice(last, m.index);
      const tok = m[0];
      if (tok.startsWith('/*') && !tok.includes('*/')) inBlock = true;
      if (tok[0] === '"' && map[tok]) {
        // no tocar concatenaciones adyacentes de literales ("A" "B") ni #include
        const before = line.slice(0, m.index), after = line.slice(m.index + tok.length);
        if (!/"\s*$/.test(before) && !/^\s*"/.test(after) && !/^\s*#\s*include/.test(line)) {
          n++; res += check ? tok : map[tok];
        } else res += tok;
      } else res += tok;
      last = m.index + tok.length;
    }
    res += line.slice(last);
    return res;
  });
  if (n) {
    perFile.push([f, n]);
    if (check) remaining += n; else { total += n; fs.writeFileSync(f, out.join(eol)); }
  }
}
for (const [f, n] of perFile) console.log(String(n).padStart(5), f);
if (check) { console.log('magic type tags remaining:', remaining); process.exit(remaining ? 1 : 0); }
console.log('replaced:', total, 'files:', perFile.length);
