// Fase 2 (0.1.x): partir from_csv_to_list — las dos fases de parseo (paralela y secuencial)
// pasan a helpers static con copy-in/copy-out de los locales que mutan. Movimiento puro.
const fs = require('fs');
const f = 'src/te_csv.c';
let src = fs.readFileSync(f, 'utf8'); const crlf = src.includes('\r\n'); if (crlf) src = src.replace(/\r\n/g, '\n');
let L = src.split('\n');
const hdr = L.findIndex(l => /^ASTNode\* from_csv_to_list\(/.test(l)); if (hdr < 0) throw new Error('hdr');
const at = o => hdr + o; // hdr es 0-based => at(o) es la línea 1-based (hdr+1)+o, como `sed -n` con s=hdr+1
// límites (verificados a mano)
const parIf = at(253), parBodyA = at(254), parBodyB = at(422), parElse = at(423), endif = at(424), seqOpen = at(425), seqBodyA = at(426), seqBodyB = at(505), seqClose = at(506);
const chk = (i, re, what) => { if (!re.test(L[i])) throw new Error(`${what}: '${L[i]}'`); };
chk(parIf, /^\s{4}if \(can_parallel >= 2\) \{$/, 'parIf'); chk(parElse, /^\s{4}\} else$/, 'parElse'); chk(endif, /^#endif$/, 'endif');
chk(seqOpen, /^\s{4}\{$/, 'seqOpen'); chk(seqClose, /^\s{4}\}$/, 'seqClose'); chk(at(252), /^#if TE_HAS_PTHREAD$/, 'if pthread');
const fix = l => l.replace(/&cfg\b/g, 'cfg').replace(/^ {4}/, ''); // &cfg -> cfg (ahora es puntero); des-indentar 4
const parBody = L.slice(parBodyA, parBodyB + 1).map(fix);
const seqBody = L.slice(seqBodyA, seqBodyB + 1).map(fix);
for (const b of [parBody, seqBody]) if (b.some(l => /\bcfg\./.test(l))) throw new Error('cfg. usado por valor; revisar');
const P = 'ClassNode *cls, char *src, size_t len, size_t pos, int nattr, CSVParseCfg *cfg, ASTNode **p_first, CSVWorkerArgs **p_worker_args, int *p_worker_args_n, TeColCache **p_worker_gcache';
const pre = ['    ASTNode *first = *p_first, *last = NULL;',
             '    CSVWorkerArgs *worker_args = *p_worker_args; int worker_args_n = *p_worker_args_n; TeColCache *worker_gcache = *p_worker_gcache;',
             '    (void)last; (void)worker_args; (void)worker_args_n;'];
const post = ['    *p_first = first; *p_worker_args = worker_args; *p_worker_args_n = worker_args_n; *p_worker_gcache = worker_gcache;'];
const helpers = [
  '#if TE_HAS_PTHREAD',
  '/* Fase de parseo PARALELA de from_csv_to_list (extraída, Fase 2). N=can_parallel workers. */',
  `static void csv_parse_parallel(int can_parallel, ${P}) {`, ...pre, ...parBody, ...post, '}', '#endif', '',
  '/* Fase de parseo SECUENCIAL de from_csv_to_list (extraída, Fase 2). */',
  `static void csv_parse_sequential(${P}) {`, ...pre, ...seqBody, ...post, '}', ''];
const A = 'cls, src, len, pos, nattr, &cfg, &first, &worker_args, &worker_args_n, &worker_gcache';
const repl = ['#if TE_HAS_PTHREAD', '    if (can_parallel >= 2) {', `        csv_parse_parallel(can_parallel, ${A});`, '    } else', '#endif', '    {', `        csv_parse_sequential(${A});`, '    }'];
L.splice(at(252), seqClose - at(252) + 1, ...repl);
// `last` solo lo usaban las fases extraídas
const iLast = L.findIndex((l, i) => i > hdr && /^\s{4}ASTNode \*first = NULL, \*last = NULL;$/.test(l));
if (iLast < 0) throw new Error('decl first/last'); L[iLast] = '    ASTNode *first = NULL;';
L.splice(hdr, 0, ...helpers);
let out = L.join('\n'); if (crlf) out = out.replace(/\n/g, '\r\n');
fs.writeFileSync(f, out);
console.log('ok: from_csv_to_list partida; helpers csv_parse_parallel/csv_parse_sequential');
