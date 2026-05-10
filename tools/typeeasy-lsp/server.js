// TypeEasy Language Server.
//
// Uses the typeeasy CLI (--syntax-check, --symbols) to compute diagnostics
// and document symbols. Talks JSON-RPC over stdio with VS Code via the
// vscode-languageserver package.
//
// Spawning typeeasy: by default we run the binary inside Docker so the
// user does not need a local toolchain. Override with env TYPEEASY_BIN
// (e.g. "/usr/local/bin/typeeasy") to use a native binary.

const {
  createConnection, ProposedFeatures, TextDocuments,
  DiagnosticSeverity, TextDocumentSyncKind,
  CompletionItemKind, SymbolKind
} = require('vscode-languageserver/node');
const { TextDocument } = require('vscode-languageserver-textdocument');
const { spawn } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const connection = createConnection(ProposedFeatures.all);
const documents = new TextDocuments(TextDocument);

const BUILTINS = [
  'println', 'print', 'fprint', 'fprintln',
  'len', 'to_int', 'to_float', 'to_str',
  'json_stringify', 'json_parse', 'http_get', 'http_post',
  'read_file', 'write_file', 'file_exists',
  'assert', 'assert_eq',
  'Math.sqrt', 'Math.abs', 'Math.floor', 'Math.ceil', 'Math.round',
  'Math.pow', 'Math.min', 'Math.max'
];
const KEYWORDS = [
  'let', 'var', 'const', 'if', 'else', 'while', 'for', 'in',
  'class', 'extends', 'new', 'return', 'true', 'false', 'null',
  'try', 'catch', 'throw', 'import', 'lambda', 'this'
];

// Per-document symbol cache: uri -> {classes, functions, variables}
const symbolCache = new Map();

connection.onInitialize(() => ({
  capabilities: {
    textDocumentSync: TextDocumentSyncKind.Full,
    completionProvider: { triggerCharacters: ['.', ' '] },
    hoverProvider: true,
    documentSymbolProvider: true
  }
}));

function runTypeEasy(args, onJson) {
  const bin = process.env.TYPEEASY_BIN;
  let cmd, fullArgs;
  if (bin) {
    cmd = bin;
    fullArgs = args;
  } else {
    // Run via docker compose from the workspace root. Assumes the user
    // has built the typeeasy image. Files are mounted at /code.
    cmd = 'docker';
    fullArgs = ['compose', 'run', '--rm', '-T', 'typeeasy', ...args];
  }
  const proc = spawn(cmd, fullArgs, { cwd: process.env.TYPEEASY_CWD || process.cwd() });
  let out = '';
  let err = '';
  proc.stdout.on('data', d => out += d.toString());
  proc.stderr.on('data', d => err += d.toString());
  proc.on('close', () => {
    try {
      // Last JSON line wins (typeeasy may emit other text too).
      const lines = out.trim().split(/\r?\n/);
      for (let i = lines.length - 1; i >= 0; i--) {
        const line = lines[i].trim();
        if (line.startsWith('{') || line.startsWith('[')) {
          onJson(JSON.parse(line));
          return;
        }
      }
      onJson(null);
    } catch (e) {
      connection.console.error(`parse error: ${e.message}\nstdout=${out}\nstderr=${err}`);
      onJson(null);
    }
  });
  proc.on('error', e => {
    connection.console.error(`spawn error (${cmd}): ${e.message}`);
    onJson(null);
  });
}

// Write the document text to a temp file inside /code (so docker mount sees it).
function writeTempFile(text) {
  const dir = process.env.TYPEEASY_TMPDIR || path.join(process.cwd(), 'typeeasycode');
  try { fs.mkdirSync(dir, { recursive: true }); } catch {}
  const name = `_lsp_${Date.now()}_${Math.random().toString(36).slice(2, 8)}.te`;
  const full = path.join(dir, name);
  fs.writeFileSync(full, text);
  return { full, name, dir };
}

function validate(doc) {
  const tmp = writeTempFile(doc.getText());
  runTypeEasy(['--syntax-check', tmp.name], (json) => {
    const diagnostics = [];
    if (json && Array.isArray(json.errors)) {
      for (const e of json.errors) {
        const line = Math.max(0, (e.line || 1) - 1);
        diagnostics.push({
          severity: DiagnosticSeverity.Error,
          range: {
            start: { line, character: 0 },
            end:   { line, character: 200 }
          },
          message: e.msg + (e.near ? ` (near '${e.near}')` : ''),
          source: 'typeeasy'
        });
      }
    }
    connection.sendDiagnostics({ uri: doc.uri, diagnostics });

    // Refresh symbols too (best-effort).
    runTypeEasy(['--symbols', tmp.name], (sym) => {
      if (sym) symbolCache.set(doc.uri, sym);
      try { fs.unlinkSync(tmp.full); } catch {}
    });
  });
}

documents.onDidChangeContent(e => validate(e.document));
documents.onDidOpen(e => validate(e.document));

connection.onCompletion((params) => {
  const items = [];
  for (const k of KEYWORDS)
    items.push({ label: k, kind: CompletionItemKind.Keyword });
  for (const b of BUILTINS)
    items.push({ label: b, kind: CompletionItemKind.Function });
  const sym = symbolCache.get(params.textDocument.uri);
  if (sym) {
    for (const c of sym.classes || [])
      items.push({ label: c.name, kind: CompletionItemKind.Class, detail: c.parent ? `extends ${c.parent}` : '' });
    for (const f of sym.functions || [])
      items.push({ label: f.name, kind: CompletionItemKind.Function, detail: f.return_type });
    for (const v of sym.variables || [])
      items.push({ label: v.name, kind: CompletionItemKind.Variable, detail: v.type });
  }
  return items;
});

connection.onHover((params) => {
  const doc = documents.get(params.textDocument.uri);
  if (!doc) return null;
  const text = doc.getText();
  const offset = doc.offsetAt(params.position);
  // Extract identifier under cursor.
  let s = offset, e = offset;
  while (s > 0 && /[A-Za-z0-9_.]/.test(text[s - 1])) s--;
  while (e < text.length && /[A-Za-z0-9_.]/.test(text[e])) e++;
  const word = text.slice(s, e);
  if (!word) return null;
  if (BUILTINS.includes(word))
    return { contents: { kind: 'markdown', value: `**${word}** _(builtin)_` } };
  if (KEYWORDS.includes(word))
    return { contents: { kind: 'markdown', value: `**${word}** _(keyword)_` } };
  const sym = symbolCache.get(params.textDocument.uri);
  if (sym) {
    for (const c of sym.classes || [])
      if (c.name === word) return { contents: { kind: 'markdown', value: `**class ${c.name}**${c.parent ? ` extends ${c.parent}` : ''}\n\nattrs: ${(c.attrs||[]).map(a=>a.name).join(', ')}` } };
    for (const f of sym.functions || [])
      if (f.name === word) return { contents: { kind: 'markdown', value: `**function ${f.name}**: ${f.return_type || 'void'}` } };
    for (const v of sym.variables || [])
      if (v.name === word) return { contents: { kind: 'markdown', value: `**${v.name}**: ${v.type}` } };
  }
  return null;
});

connection.onDocumentSymbol((params) => {
  const sym = symbolCache.get(params.textDocument.uri);
  if (!sym) return [];
  const out = [];
  const fullRange = { start: { line: 0, character: 0 }, end: { line: 0, character: 0 } };
  for (const c of sym.classes || []) {
    out.push({
      name: c.name, kind: SymbolKind.Class,
      range: fullRange, selectionRange: fullRange,
      children: (c.methods || []).map(m => ({
        name: m.name, kind: SymbolKind.Method,
        range: fullRange, selectionRange: fullRange
      }))
    });
  }
  for (const f of sym.functions || [])
    out.push({ name: f.name, kind: SymbolKind.Function, range: fullRange, selectionRange: fullRange });
  for (const v of sym.variables || [])
    out.push({ name: v.name, kind: SymbolKind.Variable, range: fullRange, selectionRange: fullRange });
  return out;
});

documents.listen(connection);
connection.listen();
