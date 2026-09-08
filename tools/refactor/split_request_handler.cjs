// request_handler: macro TE_REQ_DONE -> funcion; extraer probes y cuerpo de recuperacion de fatales.
const fs = require('fs');
const f = 'src/typeeasy_api_server.c';
let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes('\r\n'); if (crlf) s = s.replace(/\r\n/g, '\n');
let L = s.split('\n');
const S = L.findIndex(l => /^static int request_handler\(struct mg_connection \*conn, void \*cbdata\) \{/.test(l));
if (S < 0) throw new Error('rh');
// 1) recovery body: desde la linea tras "#endif" (S+179) hasta "        return 1;" (S+238) exclusive del return.
const rb0 = S + 180, rb1 = S + 237; // inclusive: g_runtime_recovery = NULL ... mg_write(conn, err, elen);
if (!/g_runtime_recovery = NULL;/.test(L[rb0]) || !/mg_write\(conn, err, elen\);/.test(L[rb1]) || !/^        return 1;/.test(L[rb1 + 1])) throw new Error('recovery bounds: ' + L[rb0] + ' | ' + L[rb1]);
const recBody = L.slice(rb0, rb1 + 1).map(l => l.replace(/TE_REQ_DONE\((\w+)\)/g, 'te_req_done(method, uri, $1, t0)'));
// 2) probes: if en S+35 hasta "    }" en S+72
const pb0 = S + 35, pb1 = S + 72;
if (!/^    if \(strcmp\(method, "GET"\) == 0 &&/.test(L[pb0]) || !/^    \}\s*$/.test(L[pb1])) throw new Error('probes bounds');
const probesBody = L.slice(pb0, pb1 + 1).map(l => l.replace(/\breturn 1;/g, 'return 1;'));
// 3) macro -> funcion (definicion en S+78..S+83)
const m0 = L.findIndex((l, i) => i > S && /^#define TE_REQ_DONE\(st\) do \{ \\$/.test(l));
let m1 = m0; while (!/\} while \(0\)\s*$/.test(L[m1])) m1++;
// aplicar de abajo hacia arriba
L.splice(rb0, rb1 - rb0 + 1, '        te_rh_respond_fatal(conn, method, uri, _req_t0);');
L.splice(m0, m1 - m0 + 1); // quitar macro
L.splice(pb0, pb1 - pb0 + 1, '    if (te_rh_probes(conn, method, uri)) return 1;');
// usos restantes del macro -> funcion
for (let i = S; i < L.length && !/^\}\s*$/.test(L[i]); i++) L[i] = L[i].replace(/TE_REQ_DONE\((\w+)\);/g, 'te_req_done(method, uri, $1, _req_t0);');
const helpers = [
  '/* Cierre de la instrumentacion por request (antes macro TE_REQ_DONE dentro de request_handler). */',
  'static void te_req_done(const char *method, const char *uri, int st, clock_t t0) {',
  '    double _ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;',
  '    __atomic_sub_fetch(&g_inflight, 1, __ATOMIC_SEQ_CST);',
  '    te_log_request(method, uri, st, _ms);',
  '    if (g_profile_enabled > 0) te_req_profile_flush(method, uri, st, _ms);',
  '}', '',
  '/* /healthz y /readyz (extraido de request_handler, Fase 2). Devuelve 1 si respondio. */',
  'static int te_rh_probes(struct mg_connection *conn, const char *method, const char *uri) {',
  ...probesBody, '    return 0;', '}', '',
  '/* Respuesta 500 tras un longjmp de te_runtime_fatalf (extraido de request_handler, Fase 2).',
  ' * Vive FUERA del frame que tiene el setjmp: menos locales ahi = unwind mas robusto (win64). */',
  'static void te_rh_respond_fatal(struct mg_connection *conn, const char *method, const char *uri, clock_t t0) {',
  ...recBody, '}', ''];
L.splice(S, 0, ...helpers);
s = L.join('\n'); if (crlf) s = s.replace(/\n/g, '\r\n'); fs.writeFileSync(f, s);
const S2 = L.findIndex(l => /^static int request_handler\(/.test(l)); let k = S2; while (!/^\}\s*$/.test(L[k])) k++;
console.log('request_handler:', k - S2, 'lineas');
