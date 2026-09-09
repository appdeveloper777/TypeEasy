// Fase 2 (0.1.x): partir main() de typeeasy_main.c en helpers por modo: version/help, emit
// wat/wasm, discover, api, invoke. Cada bloque devuelve siempre (o "no manejado" = -1). Movimiento puro.
const fs = require('fs');
const f = 'src/typeeasy_main.c';
let src = fs.readFileSync(f, 'utf8'); const crlf = src.includes('\r\n'); if (crlf) src = src.replace(/\r\n/g, '\n');
let L = src.split('\n');
const hdr = L.findIndex(l => /^int main\(int argc, char\s*\*\s*argv\[\]\)\s*\{$|^int main\(int argc, char \*\*argv\)\s*\{$/.test(l)); if (hdr < 0) throw new Error('main hdr');
const at = o => hdr + o;
const chk = (i, re, w) => { if (!re.test(L[i])) throw new Error(`${w}: '${L[i]}'`); };
chk(at(31), /^\s{4}for \(int vi = 1; vi < argc; vi\+\+\) \{$/, 'vh for'); chk(at(60), /^\s{4}\}$/, 'vh end');
chk(at(236), /^\s{4}if \(emit_wat_mode \|\| emit_wasm_mode\) \{$/, 'emit'); chk(at(263), /^\s{4}\}$/, 'emit end');
chk(at(286), /^\s{4}if \(discover_mode\) \{$/, 'disc'); chk(at(304), /^\s{4}\}$/, 'disc end');
chk(at(337), /^\s{4}if \(api_mode\) \{$/, 'api'); chk(at(381), /^\s{4}\}$/, 'api end');
chk(at(384), /^\s{4}if \(invoke_func\) \{$/, 'inv'); chk(at(411), /^\s{4}\}$/, 'inv end');
const dd = l => l.replace(/^ {4}/, '');
const body = (a, b) => L.slice(at(a) + 1, at(b)).map(dd); // interior del if (sin la línea if ni la llave)
const helpers = [
  '/* --version / --help (extraído de main, Fase 2). Devuelve 0 si atendió el flag, -1 si no. */',
  'static int te_main_version_help(int argc, char **argv, const char *TE_VERSION_STR) {',
  ...L.slice(at(31), at(61)).map(dd), '    return -1;', '}', '',
  '/* --emit-wat / --emit-wasm (extraído de main, Fase 2). */',
  'static int te_main_emit(ASTNode *script_ast, int emit_wat_mode, int emit_wasm_mode, const char *output_path) {',
  ...body(236, 263), '}', '',
  '/* --discover: lista rutas del .te como JSON (extraído de main, Fase 2). */',
  'static int te_main_discover(ASTNode *script_ast) {',
  ...body(286, 304), '}', '',
  '/* --api: levanta el servidor HTTP (extraído de main, Fase 2). */',
  'static int te_main_api(char **argv, ASTNode *script_ast, const char *script_path, int api_port, const char *api_host,',
  '                       int api_workers, int api_worker_index, const char *api_cors_origin, int dev_mode) {',
  ...body(337, 381), '}', '',
  '/* --invoke <fn>: ejecuta una función y vuelca __ret__ (extraído de main, Fase 2). 0 ok, 1 no encontrada. */',
  'static int te_main_invoke(const char *invoke_func) {',
  ...body(384, 411), '    return 0;', '}', ''];
// reemplazos de abajo hacia arriba
L.splice(at(384), 411 - 384 + 1, '    if (invoke_func && te_main_invoke(invoke_func) != 0) return 1;');
L.splice(at(337), 381 - 337 + 1, '    if (api_mode) return te_main_api(argv, script_ast, script_path, api_port, api_host, api_workers, api_worker_index, api_cors_origin, dev_mode);');
L.splice(at(286), 304 - 286 + 1, '    if (discover_mode) return te_main_discover(script_ast);');
L.splice(at(236), 263 - 236 + 1, '    if (emit_wat_mode || emit_wasm_mode) return te_main_emit(script_ast, emit_wat_mode, emit_wasm_mode, output_path);');
L.splice(at(31), 60 - 31 + 1, '    if (te_main_version_help(argc, argv, TE_VERSION_STR) == 0) return 0;');
// TE_VERSION_STR / TE_XSTR se definen dentro de main justo antes; el helper recibe el string.
L.splice(hdr, 0, ...helpers);
let out = L.join('\n'); if (crlf) out = out.replace(/\n/g, '\r\n');
fs.writeFileSync(f, out);
console.log('ok main: te_main_version_help, te_main_emit, te_main_discover, te_main_api, te_main_invoke');
