#!/usr/bin/env node
// nk_convert.cjs — strcmp(<node>->type, TE_T_X) == 0  ->  nk_of(<node>) == NK_X
//
// Conversion MECANICA y acotada (ROADMAP: "sustituir strcmp(type, TE_T_X) por nk_of(node) == NK_X
// donde el nodo ya tiene NodeKind cacheado"). Solo toca:
//   - archivos que incluyen ast_internal.h (donde nk_of es visible);
//   - receptores cuyo nombre esta en la lista blanca de variables ASTNode* (nunca Variable*);
//   - constantes TE_T_X que tienen NK_X en el enum NodeKind de ast.h.
// Semantica identica: nk_of() calcula perezosamente el kind desde ->type y lo cachea; los sitios
// que mutan ->type de un nodo existente resincronizan kind (ver te_csv.c CSV_LOAD).
// El compilador es la red: pasar un Variable* a nk_of() da "incompatible pointer type".
// Uso: node tools/refactor/nk_convert.cjs [--check]   (desde la raiz del repo)
const fs = require('fs'), path = require('path');
const root = path.resolve(__dirname, '../..');
const astH = fs.readFileSync(path.join(root, 'src/ast.h'), 'utf8');
const enumBody = astH.match(/typedef enum \{([\s\S]*?)\} NodeKind;/)[1];
const kinds = new Set([...enumBody.matchAll(/NK_[A-Z_0-9]+/g)].map(m => m[0]));
const files = ['ast.c', 'te_print.c', 'te_value.c', 'te_interp_flow.c', 'te_interp_decl.c', 'te_decimal.c', 'te_bytecode.c'];
const nodeNames = new Set(['node', 'n', 'arg', 'o', 'a', 'cur', 'item', 'val', 'value', 'src', 'lit', 'list', 'listNode', 'map', 'pair',
  'left', 'right', 'child', 'body', 'expr', 'root', 'stmt', 'elem', 'it', 'head', 'objNode', 'limit', 'step', 'fb', 'init', 'cond',
  'callee', 'target', 'first', 'valNode', 'value_node', 'argNode', 'args', 'obj', 'rhs', 'lhs', 'sub', 'inner', 'cn', 'kn', 'vn',
  'e', 'x', 'lst', 'mapNode', 'call', 'op', 'ops', 'operand', 'lam', 'lambda', 'fnNode', 'ret', 'ternary', 'then', 'els',
  'key', 'keyNode', 'idx', 'index', 'base', 'receiver', 'recv', 'method', 'chosen', 'l', 'wrapper', 'placeholder', 'w', 'nn']);
// receptores que en estos archivos son Variable* / TeValue* (nunca convertir)
const varNames = new Set(['attr', 'p', 'q', 'v', 'sv', '_v', 'r', 'attr2', 'dv', 'tv', 'kv', 'var', 'dst', 'fv', 'out', 'rv', 'ret_var', 'g_vm']);
const check = process.argv.includes('--check');
let total = 0;
const re = /(!?)strcmp\(([A-Za-z_][A-Za-z_0-9]*)->type,\s*(TE_T_[A-Z_0-9]+)\)\s*(==|!=)\s*0/g;
for (const f of files) {
  const fp = path.join(root, 'src', f);
  let s = fs.readFileSync(fp, 'utf8'), n = 0;
  s = s.replace(re, (m, neg, id, tt, cmp) => {
    const nk = 'NK_' + tt.slice(5);
    if (!kinds.has(nk) || varNames.has(id) || !nodeNames.has(id)) return m;
    // `!strcmp(...) == 0` no aparece; `!strcmp(a,b)` sin `== 0` no matchea esta regex.
    if (neg) return m;
    n++; return `nk_of(${id}) ${cmp} ${nk}`;
  });
  total += n;
  if (!check && n) fs.writeFileSync(fp, s);
  console.log(`${f}: ${n} sitios`);
}
console.log(`total: ${total}${check ? ' (solo conteo)' : ''}`);
