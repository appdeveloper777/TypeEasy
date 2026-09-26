// TypeEasy Language Server.
//
// Capabilities (declared on initialize):
//   - completion        (keywords + builtins + workspace-scanned identifiers;
//                        after `x.` -> string/list/map methods, after `Math.` -> Math.*)
//   - hover             (signature + description for builtins/methods; declarations)
//   - signatureHelp     (builtin signature while typing its arguments)
//   - formatting        (Format Document -> `typeeasy --fmt`, needs TYPEEASY_BIN)
//   - documentSymbol    (Outline view: classes/methods/functions/variables)
//   - definition        (right-click -> "Go to Definition")
//   - references        (right-click -> "Find All References")
//   - implementation    (right-click -> "Go to Implementation") — falls back
//                        to the same scanner as definition for now.
//
// Definition/references are computed by a lightweight regex scanner. It scans
// the open document AND the files reachable through the program's import graph
// (resolved from typeeasy.toml's project root / entry, matching the runtime's
// shared global scope), so cross-file global `let x = fn(...)` symbols resolve
// even when B never imports A directly. No compiler invocation required, so it
// works even when the TypeEasy binary is not installed.
//
// Optional: when env TYPEEASY_BIN points to a native typeeasy executable
// (extension.js auto-detects it on Windows/Linux installs), the server also
// runs `typeeasy --syntax-check` to surface real parser errors as
// diagnostics. There is NO Docker fallback — if the binary is missing we
// silently skip diagnostics rather than spamming the user with Docker errors.

const {
  createConnection, ProposedFeatures, TextDocuments,
  DiagnosticSeverity, TextDocumentSyncKind,
  CompletionItemKind, SymbolKind, Location, Range, Position
} = require('vscode-languageserver/node');
const { TextDocument } = require('vscode-languageserver-textdocument');
const { spawn } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const connection = createConnection(ProposedFeatures.all);
const documents = new TextDocuments(TextDocument);

// Catálogo de builtins: [nombre, firma, descripción]. Fuente: docs/STDLIB.md,
// docs/API_BUILTINS.md y docs/SPEC.md §5. Alimenta completion, hover y signatureHelp.
const BUILTIN_CATALOG = [
  ['println', 'println(x)', 'Imprime x y un salto de línea.'],
  ['print', 'print(x)', 'Imprime x sin salto de línea.'],
  ['len', 'len(x) -> int', 'Longitud de string/lista/map. Ojo 0.1.9: sobre una lista literal devuelve 0; usá una variable.'],
  ['to_int', 'to_int(x) -> int', 'Convierte a entero.'],
  ['to_float', 'to_float(x) -> float', 'Convierte a float.'],
  ['to_str', 'to_str(x) -> string', 'Convierte a string.'],
  ['range', 'range(ini, fin) -> list', 'Lista de enteros [ini, fin) (fin exclusivo).'],
  ['json', 'json(value) -> response', 'Serializa a JSON (en --api responde application/json).'],
  ['json_stringify', 'json_stringify(value) -> string', 'Serializa map/lista/objeto a JSON.'],
  ['json_parse', 'json_parse(s) -> any', 'Parsea JSON. Acceso con corchetes: p["k"]. "" -> null, inválido -> 0.'],
  ['xml', 'xml(value) -> response', 'Responde application/xml.'],
  ['concat', 'concat(a, b, ...) -> string', 'Concatena argumentos como string.'],
  ['read_file', 'read_file(path) -> string', 'Lee un archivo de texto.'],
  ['write_file', 'write_file(path, body)', 'Escribe un archivo de texto.'],
  ['file_exists', 'file_exists(path) -> bool', 'true si el archivo existe.'],
  ['env', 'env(name, default?) -> string', 'Variable de entorno.'],
  ['env_required', 'env_required(name) -> string', 'Variable de entorno obligatoria (falla si falta).'],
  ['http_get', 'http_get(url) -> string', 'GET HTTP/HTTPS.'],
  ['http_post', 'http_post(url, body) -> string', 'POST HTTP/HTTPS.'],
  ['request_param', 'request_param(name) -> string', 'Parámetro de query/ruta/form del request actual.'],
  ['request_body', 'request_body() -> string', 'Cuerpo crudo del request.'],
  ['request_header', 'request_header(name) -> string', 'Header del request.'],
  ['response_status', 'response_status(code)', 'Fija el status HTTP de la respuesta.'],
  ['jwt_sign', 'jwt_sign(payload, secret) -> string', 'Firma un JWT HS256.'],
  ['jwt_verify', 'jwt_verify(token, secret) -> bool', 'Verifica un JWT HS256.'],
  ['current_claims', 'current_claims() -> string', 'Payload del JWT validado por @auth.'],
  ['sql_connect', 'sql_connect(host, user, pass, db, port, opts, engine) -> handle', 'Conexión agnóstica de motor (engine: "mysql" | "sqlite" | "postgres" ...).'],
  ['sql_query', 'sql_query(conn, sql, params, engine) -> string', 'SELECT con parámetros; devuelve JSON de filas.'],
  ['sql_exec', 'sql_exec(conn, sql, params, engine, envelope?)', 'INSERT/UPDATE/DELETE/DDL. Con envelope=true devuelve { success, ... }.'],
  ['sql_close', 'sql_close(conn, engine)', 'Cierra la conexión.'],
  ['sql_last_error', 'sql_last_error() -> string', 'Mensaje del último fallo SQL ("" si OK).'],
  ['mysql_connect', 'mysql_connect(host, user, pass, db, opts?) -> handle', 'Conexión MySQL/MariaDB. Cloud (TiDB): { "tls": 1 }.'],
  ['mysql_query', 'mysql_query(conn, sql, "json"?) -> string', 'SQL en MySQL. El arg "json" SOLO en SELECT.'],
  ['mysql_close', 'mysql_close(conn)', 'Cierra (o devuelve al pool) la conexión.'],
  ['sqlite_connect', 'sqlite_connect(path) -> handle', 'Abre/crea una base SQLite.'],
  ['sqlite_query', 'sqlite_query(db, sql) -> rows', 'SELECT en SQLite.'],
  ['sqlite_exec', 'sqlite_exec(db, sql)', 'Escritura/DDL en SQLite (-1 si falla).'],
  ['sqlite_last_id', 'sqlite_last_id(db) -> int', 'Último rowid insertado.'],
  ['sqlite_close', 'sqlite_close(db)', 'Cierra la base SQLite.'],
  ['now', 'now() -> string', 'Fecha/hora actual.'],
  ['now_epoch', 'now_epoch() -> int', 'Epoch en segundos.'],
  ['date_parse', 'date_parse(s)', 'Parsea una fecha.'],
  ['date_format', 'date_format(t, fmt) -> string', 'Formatea una fecha.'],
  ['date_add', 'date_add(t, n, unit)', 'Suma n unidades a una fecha.'],
  ['date_diff', 'date_diff(a, b, unit) -> int', 'Diferencia entre fechas.'],
  ['uuid_v4', 'uuid_v4() -> string', 'UUID v4 aleatorio.'],
  ['uuid_valid', 'uuid_valid(s) -> bool', 'Valida un UUID.'],
  ['sha1', 'sha1(s) -> string', 'SHA-1 hex (usar en vez de SHA1() de SQL, que no es portable).'],
  ['assert', 'assert(cond, msg?)', 'Falla si cond es falso.'],
  ['assert_eq', 'assert_eq(a, b, msg?)', 'Falla si a != b.'],
  ['go', 'go(fn)', 'Lanza una tarea concurrente.'],
  ['sleep_async', 'sleep_async(ms)', 'Cede el control ms milisegundos.'],
  ['await_all', 'await_all(t1, t2, ...) -> list', 'Espera varias tareas; resultados en orden.'],
  ['Math.abs', 'Math.abs(x)', 'Valor absoluto.'],
  ['Math.floor', 'Math.floor(x)', 'Redondeo hacia abajo.'],
  ['Math.ceil', 'Math.ceil(x)', 'Redondeo hacia arriba.'],
  ['Math.round', 'Math.round(x)', 'Redondeo al entero más cercano.'],
  ['Math.trunc', 'Math.trunc(x)', 'Parte entera.'],
  ['Math.sign', 'Math.sign(x)', '-1, 0 o 1.'],
  ['Math.sqrt', 'Math.sqrt(x)', 'Raíz cuadrada.'],
  ['Math.pow', 'Math.pow(b, e)', 'Potencia.'],
  ['Math.min', 'Math.min(a, b)', 'Mínimo.'],
  ['Math.max', 'Math.max(a, b)', 'Máximo.'],
  ['Math.mod', 'Math.mod(a, b)', 'Módulo.']
];
const BUILTINS = BUILTIN_CATALOG.map(b => b[0]);
const BUILTIN_INFO = new Map(BUILTIN_CATALOG.map(b => [b[0], { sig: b[1], doc: b[2] }]));

// Métodos que se ofrecen después de un '.' (string / lista / map). Ver docs/STDLIB.md.
const METHOD_CATALOG = [
  ['replace', 'replace(a, b)', 'string', 'Reemplaza TODAS las ocurrencias.'],
  ['index_of', 'index_of(x) -> int', 'string', '-1 si no está.'],
  ['find', 'find(x) -> int', 'string', 'Alias de index_of.'],
  ['contains', 'contains(x) -> bool', 'string/lista', 'true si contiene x.'],
  ['starts_with', 'starts_with(p) -> bool', 'string', ''],
  ['ends_with', 'ends_with(s) -> bool', 'string', ''],
  ['substring', 'substring(ini, fin?)', 'string', 'fin exclusivo.'],
  ['substr', 'substr(ini, largo)', 'string', 'Por longitud.'],
  ['pad_left', 'pad_left(n, c)', 'string', 'Rellena a la izquierda ("7".pad_left(3,"0") -> "007").'],
  ['pad_right', 'pad_right(n, c)', 'string', 'Rellena a la derecha.'],
  ['repeat', 'repeat(n)', 'string', ''],
  ['char_at', 'char_at(i)', 'string', ''],
  ['char_code', 'char_code() -> int', 'string', ''],
  ['parse_int', 'parse_int() -> int', 'string', ''],
  ['parse_float', 'parse_float() -> float', 'string', ''],
  ['split', 'split(sep) -> list', 'string', ''],
  ['upper', 'upper()', 'string', ''],
  ['lower', 'lower()', 'string', ''],
  ['trim', 'trim()', 'string', ''],
  ['length', 'length', 'string/lista/map', 'Longitud (propiedad).'],
  ['push', 'push(x)', 'lista', 'Agrega al final (muta).'],
  ['pop', 'pop()', 'lista', 'Quita el último (muta).'],
  ['size', 'size() -> int', 'lista/map', ''],
  ['get', 'get(i)', 'lista', ''],
  ['join', 'join(sep) -> string', 'lista', ''],
  ['sort', 'sort()', 'lista', 'Ordena EN EL LUGAR; no devuelve la lista.'],
  ['reverse', 'reverse()', 'lista', 'Invierte EN EL LUGAR; no devuelve la lista.'],
  ['map', 'map(fn(x) => ...)', 'lista', 'Lista nueva.'],
  ['filter', 'filter(fn(x) => ...)', 'lista', 'Lista nueva.'],
  ['reduce', 'reduce(fn(acc, x) => ..., inicial)', 'lista', ''],
  ['where', 'where(fn)', 'lista', 'LINQ: filtra.'],
  ['select', 'select(fn)', 'lista', 'LINQ: proyecta.'],
  ['orderBy', 'orderBy(fn)', 'lista', 'LINQ: orden estable.'],
  ['orderByDescending', 'orderByDescending(fn)', 'lista', ''],
  ['thenBy', 'thenBy(fn)', 'lista', ''],
  ['first', 'first()', 'lista', ''],
  ['last', 'last()', 'lista', ''],
  ['firstOrDefault', 'firstOrDefault()', 'lista', ''],
  ['any', 'any(fn) -> bool', 'lista', ''],
  ['all', 'all(fn) -> bool', 'lista', ''],
  ['count', 'count() -> int', 'lista', ''],
  ['countWhere', 'countWhere(fn) -> int', 'lista', ''],
  ['sum', 'sum()', 'lista', ''],
  ['sumBy', 'sumBy(fn)', 'lista', ''],
  ['avg', 'avg()', 'lista', ''],
  ['distinct', 'distinct()', 'lista', ''],
  ['take', 'take(n)', 'lista', ''],
  ['skip', 'skip(n)', 'lista', ''],
  ['has', 'has(k) -> bool', 'map', ''],
  ['keys', 'keys() -> list', 'map', 'Orden de inserción.'],
  ['values', 'values() -> list', 'map', 'Ojo 0.1.9: .length sobre el resultado da 0; usá len(vs).'],
  ['remove', 'remove(k)', 'map', ''],
  ['clear', 'clear()', 'map', '']
];
const KEYWORDS = [
  'let', 'var', 'const', 'if', 'else', 'while', 'for', 'in',
  'class', 'extends', 'new', 'return', 'true', 'false', 'null',
  'try', 'catch', 'throw', 'import', 'lambda', 'this', 'async', 'await',
  'function', 'private', 'public', 'protected', 'from', 'as'
];

const IDENT_CHAR_RE = /[A-Za-z0-9_]/;

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------
connection.onInitialize(() => ({
  capabilities: {
    textDocumentSync: TextDocumentSyncKind.Full,
    completionProvider: { triggerCharacters: ['.', ' '] },
    hoverProvider: true,
    signatureHelpProvider: { triggerCharacters: ['(', ','] },
    documentFormattingProvider: true,
    documentSymbolProvider: true,
    definitionProvider: true,
    referencesProvider: true,
    implementationProvider: true
  }
}));

// ---------------------------------------------------------------------------
// Optional native binary (TYPEEASY_BIN). Used ONLY for diagnostics.
// No Docker fallback: missing binary -> diagnostics disabled silently.
// ---------------------------------------------------------------------------
function nativeBin() {
  const b = process.env.TYPEEASY_BIN;
  if (b && fs.existsSync(b)) return b;
  return null;
}

function runSyntaxCheck(text, onDiags) {
  const bin = nativeBin();
  if (!bin) { onDiags([]); return; }
  const tmpDir = process.env.TYPEEASY_TMPDIR || os.tmpdir();
  const tmp = path.join(tmpDir, `_lsp_${Date.now()}_${Math.random().toString(36).slice(2, 8)}.te`);
  try { fs.writeFileSync(tmp, text); } catch (e) { onDiags([]); return; }
  const proc = spawn(bin, ['--syntax-check', tmp], { cwd: process.env.TYPEEASY_CWD || process.cwd() });
  let out = '';
  proc.stdout.on('data', d => out += d.toString());
  proc.on('error', () => { try { fs.unlinkSync(tmp); } catch (_e) {} onDiags([]); });
  proc.on('close', () => {
    try { fs.unlinkSync(tmp); } catch (_e) {}
    let json = null;
    try {
      const lines = out.trim().split(/\r?\n/);
      for (let i = lines.length - 1; i >= 0; i--) {
        const line = lines[i].trim();
        if (line.startsWith('{') || line.startsWith('[')) { json = JSON.parse(line); break; }
      }
    } catch (_e) {}
    const diagnostics = [];
    const push = (list, severity) => {
      for (const e of list) {
        const line = Math.max(0, (e.line || 1) - 1);
        diagnostics.push({
          severity,
          range: { start: { line, character: 0 }, end: { line, character: 200 } },
          message: e.msg + (e.near ? ` (near '${e.near}')` : ''),
          source: 'typeeasy'
        });
      }
    };
    if (json && Array.isArray(json.errors)) push(json.errors, DiagnosticSeverity.Error);
    if (json && Array.isArray(json.warnings)) push(json.warnings, DiagnosticSeverity.Warning);
    onDiags(diagnostics);
  });
}

function validate(doc) {
  runSyntaxCheck(doc.getText(), (diagnostics) => {
    connection.sendDiagnostics({ uri: doc.uri, diagnostics });
  });
}

documents.onDidChangeContent(e => validate(e.document));
documents.onDidOpen(e => validate(e.document));

// ---------------------------------------------------------------------------
// Regex-based symbol scanner. Returns:
//   { name, kind, line, column, length, container? }
// kind: 'class' | 'method' | 'function' | 'variable' | 'parameter'
// ---------------------------------------------------------------------------
function scanSymbols(text) {
  const lines = text.split(/\r?\n/);
  const symbols = [];
  let currentClass = null;
  let braceDepth = 0;
  let classBraceDepth = -1;

  const updateBrace = (line) => {
    const stripped = line
      .replace(/\/\/.*$/g, '')
      .replace(/"(?:[^"\\]|\\.)*"/g, '""')
      .replace(/'(?:[^'\\]|\\.)*'/g, "''");
    for (let k = 0; k < stripped.length; k++) {
      const ch = stripped[k];
      if (ch === '{') braceDepth++;
      else if (ch === '}') {
        braceDepth--;
        if (currentClass && braceDepth <= classBraceDepth) {
          currentClass = null;
          classBraceDepth = -1;
        }
      }
    }
  };

  const reClass    = /^\s*(?:public\s+|private\s+|protected\s+)?class\s+([A-Za-z_][A-Za-z0-9_]*)/;
  const reFunction = /^\s*(?:public\s+|private\s+|protected\s+|async\s+)*function\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(/;
  const reMethod   = /^\s*(?:\[[^\]]*\]\s*)*(?:public\s+|private\s+|protected\s+|async\s+|static\s+)*([A-Za-z_][A-Za-z0-9_]*)\s*\([^)]*\)\s*(?::\s*[A-Za-z_][A-Za-z0-9_]*\s*)?\{/;
  const reVar      = /^\s*(?:let|var|const)\s+([A-Za-z_][A-Za-z0-9_]*)/;
  const reParamDecl= /^\s*(?:[A-Za-z_][A-Za-z0-9_]*\s+)?function\s+[A-Za-z_][A-Za-z0-9_]*\s*\(([^)]*)\)|^\s*(?:\[[^\]]*\]\s*)*(?:public\s+|private\s+|protected\s+|async\s+|static\s+)*[A-Za-z_][A-Za-z0-9_]*\s*\(([^)]*)\)\s*(?::\s*[A-Za-z_][A-Za-z0-9_]*\s*)?\{/;

  for (let i = 0; i < lines.length; i++) {
    const raw = lines[i];
    let m;

    if ((m = reClass.exec(raw))) {
      const name = m[1];
      const col = raw.indexOf(name, raw.indexOf('class'));
      symbols.push({ name, kind: 'class', line: i, column: col, length: name.length, depth: braceDepth });
      currentClass = name;
      classBraceDepth = braceDepth;
    } else if ((m = reFunction.exec(raw))) {
      const name = m[1];
      const col = raw.indexOf(name, raw.indexOf('function'));
      symbols.push({ name, kind: 'function', line: i, column: col, length: name.length, container: currentClass, depth: braceDepth });
      // Capture parameters of top-level functions.
      const pm = /\(([^)]*)\)/.exec(raw);
      if (pm) extractParams(pm[1], raw, i, currentClass, symbols);
    } else if (currentClass && (m = reMethod.exec(raw)) && !/^\s*(?:if|while|for|switch|catch|return)\s*\(/.test(raw)) {
      const name = m[1];
      if (!KEYWORDS.includes(name)) {
        const col = raw.indexOf(name);
        symbols.push({ name, kind: 'method', line: i, column: col, length: name.length, container: currentClass, depth: braceDepth });
        const pm = /\(([^)]*)\)/.exec(raw);
        if (pm) extractParams(pm[1], raw, i, currentClass, symbols);
      }
    } else if ((m = reVar.exec(raw))) {
      const name = m[1];
      const kw = m[0].trim().split(/\s+/)[0]; // let|var|const
      const col = raw.indexOf(name, raw.indexOf(kw));
      symbols.push({ name, kind: 'variable', line: i, column: col, length: name.length, container: currentClass, depth: braceDepth });
    } else if ((m = reParamDecl.exec(raw))) {
      const paramList = (m[1] || m[2] || '').trim();
      if (paramList) extractParams(paramList, raw, i, currentClass, symbols);
    }

    updateBrace(raw);
  }
  return symbols;
}

function extractParams(paramList, raw, lineIdx, container, symbols) {
  let cursor = raw.indexOf('(') + 1;
  for (const part of paramList.split(',')) {
    const partTrim = part.trim();
    const pm = /^([A-Za-z_][A-Za-z0-9_]*)/.exec(partTrim);
    if (pm) {
      const pname = pm[1];
      const idx = raw.indexOf(pname, cursor);
      if (idx >= 0) {
        symbols.push({ name: pname, kind: 'parameter', line: lineIdx, column: idx, length: pname.length, container });
        cursor = idx + pname.length;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
function symbolsFor(doc) {
  return scanSymbols(doc.getText());
}

function wordAt(doc, position) {
  const text = doc.getText();
  const offset = doc.offsetAt(position);
  let s = offset, e = offset;
  while (s > 0 && IDENT_CHAR_RE.test(text[s - 1])) s--;
  while (e < text.length && IDENT_CHAR_RE.test(text[e])) e++;
  if (s === e) return null;
  return { word: text.slice(s, e), start: s, end: e };
}

function findDefinitions(symbols, name) {
  const prio = { class: 0, function: 1, method: 2, variable: 3, parameter: 4 };
  return symbols
    .filter(s => s.name === name)
    .sort((a, b) => (prio[a.kind] - prio[b.kind]) || (a.line - b.line));
}

function symRangeToLspRange(sym) {
  return {
    start: { line: sym.line, character: sym.column },
    end:   { line: sym.line, character: sym.column + sym.length }
  };
}

function escapeRegex(s) {
  return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

// ---------------------------------------------------------------------------
// Cross-file globals via the import graph.
//
// TypeEasy shares ONE global scope across every imported file, in import
// order. Imports are resolved relative to the project root (the directory
// holding typeeasy.toml), matching the interpreter. We follow the import graph
// from the project's entry file (typeeasy.toml `entry`) AND from the current
// file, so an "aggregator" main.te that imports A then B lets B resolve a
// global defined in A even when B never imports A directly.
// ---------------------------------------------------------------------------
const IMPORT_RE = /^\s*import\s+["']([^"']+)["']/;

function normPath(p) {
  let n = path.normalize(p);
  if (process.platform === 'win32') n = n.toLowerCase();
  return n;
}

function uriToFsPath(uri) {
  if (typeof uri !== 'string' || !uri.startsWith('file://')) return null;
  let p = uri.slice('file://'.length);
  try { p = decodeURIComponent(p); } catch (_e) {}
  if (process.platform === 'win32') {
    if (/^\/[A-Za-z]:/.test(p)) p = p.slice(1);
    p = p.replace(/\//g, '\\');
  }
  return p;
}

function fsPathToUri(fsPath) {
  let p = path.resolve(fsPath).replace(/\\/g, '/');
  if (/^[A-Za-z]:/.test(p)) p = p[0].toLowerCase() + p.slice(1); // canonical VS Code casing
  if (!p.startsWith('/')) p = '/' + p;
  return 'file://' + p.split('/').map(encodeURIComponent).join('/');
}

function findOpenDocByPath(fsPath) {
  const target = normPath(fsPath);
  for (const d of documents.all()) {
    const dp = uriToFsPath(d.uri);
    if (dp && normPath(dp) === target) return d;
  }
  return null;
}

function extractImports(text) {
  const out = [];
  for (const line of text.split(/\r?\n/)) {
    const m = IMPORT_RE.exec(line);
    if (m) out.push(m[1]);
  }
  return out;
}

// Per-file cache of scanned symbols + import specs, invalidated by mtime.
// Open documents always use their live (unsaved) text and bypass the cache.
const fileAnalysisCache = new Map();

function analyzeFile(fsPath) {
  const open = findOpenDocByPath(fsPath);
  if (open) {
    const text = open.getText();
    return { symbols: scanSymbols(text), imports: extractImports(text) };
  }
  let st;
  try { st = fs.statSync(fsPath); } catch (_e) { return { symbols: [], imports: [] }; }
  const key = normPath(fsPath);
  const cached = fileAnalysisCache.get(key);
  if (cached && cached.mtimeMs === st.mtimeMs) return cached;
  let text;
  try { text = fs.readFileSync(fsPath, 'utf8'); } catch (_e) { return { symbols: [], imports: [] }; }
  const entry = { mtimeMs: st.mtimeMs, symbols: scanSymbols(text), imports: extractImports(text) };
  fileAnalysisCache.set(key, entry);
  return entry;
}

function findProjectRoot(startFsPath) {
  let dir = path.dirname(startFsPath);
  for (let i = 0; i < 64; i++) {
    try { if (fs.existsSync(path.join(dir, 'typeeasy.toml'))) return dir; } catch (_e) {}
    const parent = path.dirname(dir);
    if (!parent || parent === dir) break;
    dir = parent;
  }
  return null;
}

function entryFileForRoot(projectRoot) {
  try {
    const toml = fs.readFileSync(path.join(projectRoot, 'typeeasy.toml'), 'utf8');
    const m = /^\s*entry\s*=\s*["']([^"']+)["']/m.exec(toml);
    if (m) {
      const p = path.resolve(projectRoot, m[1]);
      if (fs.existsSync(p)) return p;
    }
  } catch (_e) {}
  return null;
}

function resolveImportSpec(spec, projectRoot, importerDir) {
  const tries = [];
  if (projectRoot) tries.push(path.resolve(projectRoot, spec));
  tries.push(path.resolve(importerDir, spec)); // fallback: relative to importer
  for (const t of tries) { try { if (fs.existsSync(t)) return t; } catch (_e) {} }
  return tries[0];
}

function importClosure(startFsPaths, projectRoot) {
  const seen = new Set();
  const files = [];
  const queue = startFsPaths.slice();
  while (queue.length && files.length < 4000) {
    const f = queue.shift();
    if (!f) continue;
    const key = normPath(f);
    if (seen.has(key)) continue;
    seen.add(key);
    files.push(f);
    const dir = path.dirname(f);
    for (const spec of analyzeFile(f).imports) {
      queue.push(resolveImportSpec(spec, projectRoot, dir));
    }
  }
  return files;
}

function importStartsForDoc(doc) {
  const fsPath = uriToFsPath(doc.uri);
  if (!fsPath) return { fsPath: null, projectRoot: null, starts: [] };
  const projectRoot = findProjectRoot(fsPath);
  const starts = [];
  if (projectRoot) {
    const entry = entryFileForRoot(projectRoot);
    if (entry) starts.push(entry);
  }
  starts.push(fsPath);
  return { fsPath, projectRoot, starts };
}

// Top-level globals visible to `doc` through the shared program scope.
// Returns [{ uri, sym }]; the current file carries doc.uri verbatim.
function projectGlobalSymbols(doc) {
  const { fsPath, projectRoot, starts } = importStartsForDoc(doc);
  if (!fsPath) return [];
  const out = [];
  for (const f of importClosure(starts, projectRoot)) {
    const uri = (normPath(f) === normPath(fsPath)) ? doc.uri : fsPathToUri(f);
    for (const s of analyzeFile(f).symbols) {
      if (s.container) continue;          // class members are not global
      if (s.depth && s.depth > 0) continue; // block/function locals are not global
      if (s.kind === 'parameter') continue;
      out.push({ uri, sym: s });
    }
  }
  return out;
}

function displayPathForUri(uri, doc) {
  const p = uriToFsPath(uri);
  if (!p) return uri;
  const base = uriToFsPath(doc.uri);
  const root = base ? findProjectRoot(base) : null;
  if (root) {
    const rel = path.relative(root, p);
    if (rel && !rel.startsWith('..')) return rel.replace(/\\/g, '/');
  }
  return path.basename(p);
}

// A watched .te file changed on disk -> drop cached analyses.
connection.onDidChangeWatchedFiles(() => { fileAnalysisCache.clear(); });

// ---------------------------------------------------------------------------
// Completion
// ---------------------------------------------------------------------------
connection.onCompletion((params) => {
  const items = [];
  const doc = documents.get(params.textDocument.uri);
  // Después de `x.` ofrecer métodos de string/lista/map (salvo `Math.`).
  if (doc) {
    const off = doc.offsetAt(params.position);
    const before = doc.getText().slice(Math.max(0, off - 80), off);
    const m = /([A-Za-z0-9_\])"']+)\.([A-Za-z_]*)$/.exec(before);
    if (m) {
      if (m[1] === 'Math') {
        for (const b of BUILTIN_CATALOG.filter(x => x[0].startsWith('Math.')))
          items.push({ label: b[0].slice(5), kind: CompletionItemKind.Function, detail: b[1], documentation: b[2] });
        return items;
      }
      for (const [name, sig, on, d] of METHOD_CATALOG)
        items.push({ label: name, kind: name === 'length' ? CompletionItemKind.Property : CompletionItemKind.Method,
                     detail: `${sig}  (${on})`, documentation: d || undefined });
      return items;
    }
  }
  for (const k of KEYWORDS)
    items.push({ label: k, kind: CompletionItemKind.Keyword });
  for (const b of BUILTIN_CATALOG)
    items.push({ label: b[0], kind: CompletionItemKind.Function, detail: b[1], documentation: b[2] });
  if (doc) {
    const seen = new Set();
    const addSym = (s, container) => {
      const key = s.name + ':' + s.kind;
      if (seen.has(key)) return;
      seen.add(key);
      let kind = CompletionItemKind.Variable;
      if (s.kind === 'class') kind = CompletionItemKind.Class;
      else if (s.kind === 'function' || s.kind === 'method') kind = CompletionItemKind.Function;
      items.push({ label: s.name, kind, detail: s.kind + (container ? ` in ${container}` : '') });
    };
    for (const s of symbolsFor(doc)) addSym(s, s.container);
    for (const e of projectGlobalSymbols(doc)) if (e.uri !== doc.uri) addSym(e.sym, null);
  }
  return items;
});

// ---------------------------------------------------------------------------
// Hover
// ---------------------------------------------------------------------------
connection.onHover((params) => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return null;
  const w = wordAt(doc, params.position);
  if (!w) return null;
  // 1) Local declaration in the current document.
  const defs = findDefinitions(symbolsFor(doc), w.word);
  if (defs.length > 0) {
    const d = defs[0];
    let label = `**${d.kind} ${d.name}**`;
    if (d.container) label += ` _(in ${d.container})_`;
    label += `\n\nDeclared at line ${d.line + 1}`;
    return { contents: { kind: 'markdown', value: label } };
  }
  // 2) Cross-file global reachable through the import graph.
  const cross = projectGlobalSymbols(doc).filter(e => e.sym.name === w.word && e.uri !== doc.uri);
  if (cross.length > 0) {
    const e = cross[0];
    const label = `**${e.sym.kind} ${e.sym.name}**\n\nDeclared in \`${displayPathForUri(e.uri, doc)}\` at line ${e.sym.line + 1}`;
    return { contents: { kind: 'markdown', value: label } };
  }
  // 3) Builtin / keyword fallback (no definition site).
  const text = doc.getText();
  const qualified = (w.start >= 5 && text.slice(w.start - 5, w.start) === 'Math.') ? 'Math.' + w.word : w.word;
  const info = BUILTIN_INFO.get(qualified);
  if (info)
    return { contents: { kind: 'markdown', value: '```te\n' + info.sig + '\n```\n' + info.doc + '\n\n_(builtin)_' } };
  const meth = (w.start > 0 && text[w.start - 1] === '.') ? METHOD_CATALOG.find(x => x[0] === w.word) : null;
  if (meth)
    return { contents: { kind: 'markdown', value: '```te\n.' + meth[1] + '\n```\n' + (meth[3] || '') + ` _(método de ${meth[2]})_` } };
  if (KEYWORDS.includes(w.word))
    return { contents: { kind: 'markdown', value: `**${w.word}** _(keyword)_` } };
  return null;
});

// ---------------------------------------------------------------------------
// Document symbols (Outline)
// ---------------------------------------------------------------------------
connection.onDocumentSymbol((params) => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return [];
  const syms = symbolsFor(doc);
  const classes = new Map();
  const result = [];

  const kindMap = {
    class: SymbolKind.Class,
    method: SymbolKind.Method,
    function: SymbolKind.Function,
    variable: SymbolKind.Variable,
    parameter: SymbolKind.Variable
  };

  for (const s of syms) {
    if (s.kind === 'parameter') continue;
    const range = symRangeToLspRange(s);
    const node = {
      name: s.name,
      kind: kindMap[s.kind] || SymbolKind.Variable,
      range,
      selectionRange: range,
      children: []
    };
    if (s.kind === 'class') {
      classes.set(s.name, node);
      result.push(node);
    } else if (s.kind === 'method' && s.container && classes.has(s.container)) {
      classes.get(s.container).children.push(node);
    } else if (!s.container) {
      result.push(node);
    }
  }
  return result;
});

// ---------------------------------------------------------------------------
// Definition / Implementation
// ---------------------------------------------------------------------------
function definitionLocations(params) {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return [];
  const w = wordAt(doc, params.position);
  if (!w) return [];
  if (KEYWORDS.includes(w.word)) return [];
  // 1) Local definitions in the current document.
  const localDefs = findDefinitions(symbolsFor(doc), w.word);
  if (localDefs.length > 0) {
    return localDefs.map(d => Location.create(doc.uri, symRangeToLspRange(d)));
  }
  // 2) Cross-file globals reachable through the import graph.
  const prio = { class: 0, function: 1, variable: 2 };
  const cross = projectGlobalSymbols(doc)
    .filter(e => e.sym.name === w.word && e.uri !== doc.uri)
    .sort((a, b) => (prio[a.sym.kind] ?? 9) - (prio[b.sym.kind] ?? 9) || a.sym.line - b.sym.line);
  return cross.map(e => Location.create(e.uri, symRangeToLspRange(e.sym)));
}

connection.onDefinition(definitionLocations);
connection.onImplementation(definitionLocations);

// ---------------------------------------------------------------------------
// References
// ---------------------------------------------------------------------------
connection.onReferences((params) => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return [];
  const w = wordAt(doc, params.position);
  if (!w) return [];
  if (KEYWORDS.includes(w.word) || BUILTINS.includes(w.word)) return [];
  const locations = [];
  const scanText = (text, uri) => {
    const re = new RegExp(`(?<![A-Za-z0-9_])${escapeRegex(w.word)}(?![A-Za-z0-9_])`, 'g');
    const lines = text.split(/\r?\n/);
    for (let i = 0; i < lines.length; i++) {
      const stripped = lines[i].replace(/\/\/.*$/, m => ' '.repeat(m.length));
      let m;
      while ((m = re.exec(stripped)) !== null) {
        locations.push(Location.create(uri, Range.create(
          Position.create(i, m.index),
          Position.create(i, m.index + w.word.length)
        )));
      }
    }
  };
  // Current document (live text).
  scanText(doc.getText(), doc.uri);
  // Every other file in the program's import closure.
  const { fsPath, projectRoot, starts } = importStartsForDoc(doc);
  if (fsPath) {
    const done = new Set([normPath(fsPath)]);
    for (const f of importClosure(starts, projectRoot)) {
      const key = normPath(f);
      if (done.has(key)) continue;
      done.add(key);
      const open = findOpenDocByPath(f);
      let text = null;
      if (open) text = open.getText();
      else { try { text = fs.readFileSync(f, 'utf8'); } catch (_e) {} }
      if (text != null) scanText(text, fsPathToUri(f));
    }
  }
  return locations;
});

// ---------------------------------------------------------------------------
// Signature help: firma del builtin cuyo '(' está abierto antes del cursor.
// ---------------------------------------------------------------------------
connection.onSignatureHelp((params) => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return null;
  const off = doc.offsetAt(params.position);
  const text = doc.getText().slice(Math.max(0, off - 400), off);
  let depth = 0, commas = 0;
  for (let i = text.length - 1; i >= 0; i--) {
    const ch = text[i];
    if (ch === ')' || ch === ']' || ch === '}') depth++;
    else if (ch === '[' || ch === '{') { if (depth === 0) return null; depth--; }
    else if (ch === '(') {
      if (depth > 0) { depth--; continue; }
      const m = /((?:Math\.)?[A-Za-z_][A-Za-z0-9_]*)\s*$/.exec(text.slice(0, i));
      const info = m && BUILTIN_INFO.get(m[1]);
      if (!info) return null;
      const inner = /\(([^)]*)\)/.exec(info.sig);
      const ps = inner ? inner[1].split(',').map(s => s.trim()).filter(Boolean) : [];
      return {
        signatures: [{ label: info.sig, documentation: info.doc, parameters: ps.map(p => ({ label: p })) }],
        activeSignature: 0,
        activeParameter: Math.min(commas, Math.max(0, ps.length - 1))
      };
    } else if (ch === ',' && depth === 0) commas++;
    else if (ch === ';') return null;
  }
  return null;
});

// ---------------------------------------------------------------------------
// Formatting: delega en `typeeasy --fmt <archivo>` (imprime el resultado por
// stdout). Sin binario -> no formatea (devuelve []).
// ---------------------------------------------------------------------------
connection.onDocumentFormatting((params) => new Promise((resolve) => {
  const doc = documents.get(params.textDocument.uri);
  const bin = nativeBin();
  if (!doc || !bin) { resolve([]); return; }
  const original = doc.getText();
  const tmpDir = process.env.TYPEEASY_TMPDIR || os.tmpdir();
  const tmp = path.join(tmpDir, `_lsp_fmt_${Date.now()}_${Math.random().toString(36).slice(2, 8)}.te`);
  try { fs.writeFileSync(tmp, original); } catch (_e) { resolve([]); return; }
  const proc = spawn(bin, ['--fmt', tmp], { cwd: process.env.TYPEEASY_CWD || process.cwd() });
  let out = '';
  proc.stdout.on('data', d => out += d.toString());
  proc.on('error', () => { try { fs.unlinkSync(tmp); } catch (_e) {} resolve([]); });
  proc.on('close', (code) => {
    try { fs.unlinkSync(tmp); } catch (_e) {}
    if (code !== 0 || !out || out === original) { resolve([]); return; }
    const end = doc.positionAt(original.length);
    resolve([{ range: { start: { line: 0, character: 0 }, end }, newText: out }]);
  });
}));

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
documents.listen(connection);
connection.listen();
