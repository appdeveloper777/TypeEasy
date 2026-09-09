// Fase 2 (0.1.x): partir native_mysql_connect (mysql_bridge.c): parseo del map de opciones y
// configuración TLS a helpers static con una struct MysqlTlsOpts. Movimiento puro.
const fs = require('fs');
const f = 'src/mysql_bridge.c';
let src = fs.readFileSync(f, 'utf8'); const crlf = src.includes('\r\n'); if (crlf) src = src.replace(/\r\n/g, '\n');
let L = src.split('\n');
const hdr = L.findIndex(l => /^[A-Za-z_].*\bnative_mysql_connect\(.*\)\s*\{$/.test(l)); if (hdr < 0) throw new Error('hdr');
const at = o => hdr + o;
const chk = (i, re, w) => { if (!re.test(L[i])) throw new Error(`${w}: '${L[i]}'`); };
chk(at(92), /^\s{4}int opt_tls = -1;/, 'opt_tls'); chk(at(96), /^\s{4}int opt_tls_insecure = 0;/, 'insecure');
chk(at(97), /^\s{4}int opts_owned = 0;$/, 'owned'); chk(at(98), /^\s{4}ASTNode\* opts_head = db_arg_as_map_head\(args, 5, &opts_owned\);$/, 'head');
chk(at(99), /^\s{4}for \(ASTNode\* p = opts_head; p; p = p->right\) \{$/, 'for opts'); chk(at(171), /^\s{4}\}$/, 'for end');
chk(at(183), /^\s{4}int tls_enabled = 0;$/, 'tls_enabled'); chk(at(198), /^\s{4}unsigned long client_flags = 0;$/, 'client_flags');
chk(at(292), /^\s{4}\}$/, 'tls end'); chk(at(294), /^\s{4}if \(opts_owned && opts_head\) free_ast\(opts_head\);$/, 'free opts');
const dd = l => l.replace(/^ {4}/, '');
const ren = l => l.replace(/\bopt_tls_version\b/g, 'o->tls_version').replace(/\bopt_tls_fp\b/g, 'o->tls_fp')
  .replace(/\bopt_tls_ca\b/g, 'o->tls_ca').replace(/\bopt_tls_insecure\b/g, 'o->tls_insecure').replace(/\bopt_tls\b/g, 'o->tls');
const optsLoop = L.slice(at(99), at(172)).map(dd).map(ren);
const tlsBlock = L.slice(at(183), at(293)).filter(l => !/^\s{4}unsigned long client_flags = 0;$/.test(l))
  .map(dd).map(ren).map(l => l.replace(/\bclient_flags\b/g, '(*client_flags)'));
const helpers = [
  '/* Opciones TLS del 6º argumento de mysql_connect ({ tls, tls_version, tls_fp, tls_ca, tls_insecure }). */',
  'typedef struct { int tls; const char *tls_version; const char *tls_fp; const char *tls_ca; int tls_insecure; } MysqlTlsOpts;',
  '',
  '/* Lee el map de opciones (extraído de native_mysql_connect, Fase 2). */',
  'static void mysql_parse_opts(ASTNode *opts_head, MysqlTlsOpts *o) {',
  ...optsLoop, '}', '',
  '/* Aplica las opciones TLS al handle antes de mysql_real_connect (extraído de native_mysql_connect, Fase 2). */',
  'static void mysql_apply_tls(MYSQL *conn, const MysqlTlsOpts *o, unsigned long *client_flags) {',
  ...tlsBlock, '}', ''];
// reemplazos de abajo hacia arriba: bloque TLS (183..292) -> llamada; conserva la decl de client_flags
L.splice(at(183), 292 - 183 + 1, '    unsigned long client_flags = 0;', '    mysql_apply_tls(conn, &o, &client_flags);');
// loop de opciones (99..171) -> llamada
L.splice(at(99), 171 - 99 + 1, '    mysql_parse_opts(opts_head, &o);');
// decls opt_* (92..96) -> struct
L.splice(at(92), 96 - 92 + 1, '    MysqlTlsOpts o = { -1, NULL, NULL, NULL, 0 };   /* tls=-1: no especificado por el script */');
L.splice(hdr, 0, ...helpers);
let out = L.join('\n'); if (crlf) out = out.replace(/\n/g, '\r\n');
fs.writeFileSync(f, out);
console.log('ok mysql: mysql_parse_opts, mysql_apply_tls');
