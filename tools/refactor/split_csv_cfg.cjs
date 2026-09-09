// Fase 2 (0.1.x): from_csv_to_list — la fase de clasificación de atributos / mapeo de columnas /
// arenas / row template pasa a csv_build_cfg(); el resto de la función usa cfg.* . Movimiento puro.
const fs = require('fs');
const f = 'src/te_csv.c';
let src = fs.readFileSync(f, 'utf8'); const crlf = src.includes('\r\n'); if (crlf) src = src.replace(/\r\n/g, '\n');
let L = src.split('\n');
const hdr = L.findIndex(l => /^ASTNode\* from_csv_to_list\(/.test(l)); if (hdr < 0) throw new Error('hdr');
const end = L.findIndex((l, i) => i > hdr && /^\}$/.test(l));
const at = o => hdr + o;
const chk = (i, re, what) => { if (!re.test(L[i])) throw new Error(`${what}: '${L[i]}'`); };
chk(at(72), /^\s{4}int nattr = cls->attr_count;$/, 'nattr');
chk(at(76), /^\s{4}enum \{ K_INT = 0/, 'enum');
chk(at(156), /^\s{4}cfg\.row_template_bytes = /, 'row_template_bytes');
chk(at(141), /^\s{4}CSVParseCfg cfg;$/, 'cfg decl');
// cuerpo del helper: 74..157 (comentario previo al enum incluido), sin la decl `CSVParseCfg cfg;`
const body = L.slice(at(74), at(157)).filter((l, i) => !/^\s{4}CSVParseCfg cfg;$/.test(l))
  .map(l => l.replace(/^\s{4}cfg\./, '    cfg->'));
// el error fatal libera lo alocado: header lo libera el caller? no: lo hacía inline; se mantiene igual (mismo código)
const helper = [
  '/* Fase de configuración de from_csv_to_list (extraída, Fase 2): clasifica atributos, mapea',
  ' * columnas CSV -> atributo, arenas compartidas y row template. Termina con te_runtime_fatal()',
  ' * si falta una columna no-nullable. Lo alocado se libera en from_csv_to_list vía cfg->*. */',
  'static void csv_build_cfg(ClassNode *cls, char **header, int header_n, const char *filename, CSVParseCfg *cfg) {',
  '    int nattr = cls->attr_count;',
  ...body,
  '}', ''];
L.splice(at(74), 157 - 74, '    CSVParseCfg cfg;', '    csv_build_cfg(cls, header, header_n, filename, &cfg);');
// resto de la función: locales -> cfg.*
const map = { attr_kind: 'cfg.attr_kind', attr_nullable: 'cfg.attr_nullable', col_to_attr: 'cfg.col_to_attr',
  shared_attr_id_arena: 'cfg.shared_attr_id', shared_attr_type_arena: 'cfg.shared_attr_type' };
const end2 = L.findIndex((l, i) => i > hdr && /^\}$/.test(l));
for (let i = at(76); i <= end2; i++) for (const k of Object.keys(map)) L[i] = L[i].replace(new RegExp('(?<![\\w.>])' + k + '\\b', 'g'), map[k]);
L.splice(hdr, 0, ...helper);
let out = L.join('\n'); if (crlf) out = out.replace(/\n/g, '\r\n');
fs.writeFileSync(f, out);
console.log('ok csv_build_cfg');
