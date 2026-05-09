// VS Code extension entry: registers the TypeEasy DebugAdapter (inline mode).
const vscode = require('vscode');
const { TypeEasyDebugSession } = require('./debugSession');

class InlineFactory {
  createDebugAdapterDescriptor(_session) {
    return new vscode.DebugAdapterInlineImplementation(new TypeEasyDebugSession());
  }
}

function activate(context) {
  context.subscriptions.push(
    vscode.debug.registerDebugAdapterDescriptorFactory('typeeasy', new InlineFactory())
  );
}

function deactivate() {}

module.exports = { activate, deactivate };
