// Smoke test del Language Server: `node tools/typeeasy-lsp/smoke_test.js`
// (opcional: TYPEEASY_BIN=<typeeasy> para probar también el formateo con --fmt).
const { spawn } = require('child_process');
const path = require('path');

const srv = spawn(process.execPath, [path.join(__dirname, 'server.js'), '--stdio'], {
  env: process.env, stdio: ['pipe', 'pipe', 'inherit']
});
let buf = Buffer.alloc(0);
const pending = new Map();
let seq = 0;
srv.stdout.on('data', (d) => {
  buf = Buffer.concat([buf, d]);
  for (;;) {
    const sep = buf.indexOf('\r\n\r\n');
    if (sep < 0) return;
    const len = parseInt(/Content-Length: (\d+)/i.exec(buf.slice(0, sep).toString())[1], 10);
    if (buf.length < sep + 4 + len) return;
    const msg = JSON.parse(buf.slice(sep + 4, sep + 4 + len).toString());
    buf = buf.slice(sep + 4 + len);
    if (msg.id != null && pending.has(msg.id)) { pending.get(msg.id)(msg.result); pending.delete(msg.id); }
  }
});
const send = (m) => { const s = JSON.stringify(m); srv.stdin.write(`Content-Length: ${Buffer.byteLength(s)}\r\n\r\n${s}`); };
const req = (method, params) => new Promise((res) => { const id = ++seq; pending.set(id, res); send({ jsonrpc: '2.0', id, method, params }); });
const note = (method, params) => send({ jsonrpc: '2.0', method, params });

const uri = 'file:///tmp/smoke.te';
const text = 'let s = "7";\nlet t = s.pad_left(3, "0");\nlet p = json_parse(t);\nlet   x=1;\n';
const fails = [];
const check = (name, cond) => { console.log((cond ? 'PASS ' : 'FAIL ') + name); if (!cond) fails.push(name); };

(async () => {
  await req('initialize', { processId: null, rootUri: null, capabilities: {} });
  note('initialized', {});
  note('textDocument/didOpen', { textDocument: { uri, languageId: 'typeeasy', version: 1, text } });
  const comp = await req('textDocument/completion', { textDocument: { uri }, position: { line: 1, character: 10 } });
  const labels = (comp || []).map(i => i.label);
  check('completion tras "s." ofrece pad_left/replace', labels.includes('pad_left') && labels.includes('replace') && !labels.includes('let'));
  const comp2 = await req('textDocument/completion', { textDocument: { uri }, position: { line: 2, character: 8 } });
  check('completion general ofrece builtins con firma', (comp2 || []).some(i => i.label === 'json_parse' && /json_parse\(s\)/.test(i.detail || '')));
  const hov = await req('textDocument/hover', { textDocument: { uri }, position: { line: 1, character: 12 } });
  check('hover de metodo pad_left', !!hov && /pad_left\(n, c\)/.test(hov.contents.value));
  const sig = await req('textDocument/signatureHelp', { textDocument: { uri }, position: { line: 2, character: 19 } });
  check('signatureHelp de json_parse', !!sig && /json_parse/.test(sig.signatures[0].label));
  if (process.env.TYPEEASY_BIN) {
    const fmt = await req('textDocument/formatting', { textDocument: { uri }, options: { tabSize: 4, insertSpaces: true } });
    check('formatting via --fmt', Array.isArray(fmt) && fmt.length === 1 && /let x = 1;/.test(fmt[0].newText));
  }
  srv.kill();
  process.exit(fails.length ? 1 : 0);
})();
