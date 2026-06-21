// VS Code extension entry: registers the TypeEasy DebugAdapter (inline mode)
// and the TypeEasy Language Client (LSP).
const vscode = require('vscode');
const path = require('path');
const fs = require('fs');
const { TypeEasyDebugSession } = require('./debugSession');

let client = null;

class InlineFactory {
  createDebugAdapterDescriptor(_session) {
    return new vscode.DebugAdapterInlineImplementation(new TypeEasyDebugSession());
  }
}

// Locate the LSP server.js. When packaged in the .vsix it lives at
// <ext>/lsp/server.js (copied there by scripts/build_vscode_vsix.sh).
// In a cloned-repo dev checkout it lives at ../typeeasy-lsp/server.js.
function findServerModule() {
  const candidates = [
    path.resolve(__dirname, 'lsp', 'server.js'),
    path.resolve(__dirname, '..', 'typeeasy-lsp', 'server.js')
  ];
  for (const c of candidates) { if (fs.existsSync(c)) return c; }
  return null;
}

// Locate a native typeeasy binary so the LSP can run --syntax-check without
// Docker. Order: user setting, env var, well-known install paths.
function findNativeBin() {
  const cfg = vscode.workspace.getConfiguration('typeeasy');
  const setting = cfg.get('binaryPath');
  if (setting && fs.existsSync(setting)) return setting;
  if (process.env.TYPEEASY_BIN && fs.existsSync(process.env.TYPEEASY_BIN)) return process.env.TYPEEASY_BIN;

  const candidates = [];
  if (process.platform === 'win32') {
    const pf = process.env['ProgramFiles'] || 'C:\\Program Files';
    const pfx86 = process.env['ProgramFiles(x86)'] || 'C:\\Program Files (x86)';
    candidates.push(
      path.join(pf, 'TypeEasy', 'bin', 'typeeasy-bin.exe'),
      path.join(pfx86, 'TypeEasy', 'bin', 'typeeasy-bin.exe')
    );
  } else {
    candidates.push(
      '/usr/bin/typeeasy-server',
      '/usr/bin/typeeasy-bin',
      '/usr/local/bin/typeeasy-server',
      '/usr/local/bin/typeeasy-bin'
    );
  }
  for (const c of candidates) { if (fs.existsSync(c)) return c; }
  return null;
}

function startLanguageClient(context) {
  let LanguageClient, TransportKind;
  try {
    ({ LanguageClient, TransportKind } = require('vscode-languageclient/node'));
  } catch (e) {
    vscode.window.showWarningMessage(
      'TypeEasy: vscode-languageclient not bundled. Reinstall the extension from the latest TypeEasy installer.'
    );
    return;
  }
  const serverModule = findServerModule();
  if (!serverModule) {
    vscode.window.showWarningMessage(
      'TypeEasy: LSP server.js not found inside the extension. Right-click navigation will not work. Reinstall the extension.'
    );
    return;
  }
  const folders = vscode.workspace.workspaceFolders;
  const cwd = folders && folders[0] ? folders[0].uri.fsPath : process.cwd();
  const env = Object.assign({}, process.env, { TYPEEASY_CWD: cwd });
  const bin = findNativeBin();
  if (bin) env.TYPEEASY_BIN = bin;
  const serverOptions = {
    run:   { module: serverModule, transport: TransportKind.ipc, options: { env } },
    debug: { module: serverModule, transport: TransportKind.ipc, options: { env, execArgv: ['--nolazy', '--inspect=6009'] } }
  };
  const clientOptions = {
    documentSelector: [{ scheme: 'file', language: 'typeeasy' }],
    synchronize: { fileEvents: vscode.workspace.createFileSystemWatcher('**/*.te') },
    outputChannelName: 'TypeEasy LSP'
  };
  client = new LanguageClient('typeeasy', 'TypeEasy LSP', serverOptions, clientOptions);
  client.start();
  context.subscriptions.push({ dispose: () => client && client.stop() });
}

function activate(context) {
  context.subscriptions.push(
    vscode.debug.registerDebugAdapterDescriptorFactory('typeeasy', new InlineFactory())
  );
  startLanguageClient(context);
}

function deactivate() {
  return client ? client.stop() : undefined;
}

module.exports = { activate, deactivate };
