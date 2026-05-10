// VS Code extension entry: registers the TypeEasy DebugAdapter (inline mode)
// and the TypeEasy Language Client (LSP).
const vscode = require('vscode');
const path = require('path');
const { TypeEasyDebugSession } = require('./debugSession');

let client = null;

class InlineFactory {
  createDebugAdapterDescriptor(_session) {
    return new vscode.DebugAdapterInlineImplementation(new TypeEasyDebugSession());
  }
}

function startLanguageClient(context) {
  let LanguageClient, TransportKind;
  try {
    ({ LanguageClient, TransportKind } = require('vscode-languageclient/node'));
  } catch (e) {
    vscode.window.showWarningMessage(
      'TypeEasy: vscode-languageclient not installed. Run `npm install` in tools/typeeasy-vscode and tools/typeeasy-lsp.'
    );
    return;
  }
  const serverModule = path.resolve(__dirname, '..', 'typeeasy-lsp', 'server.js');
  const folders = vscode.workspace.workspaceFolders;
  const cwd = folders && folders[0] ? folders[0].uri.fsPath : process.cwd();
  const env = Object.assign({}, process.env, { TYPEEASY_CWD: cwd });
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
