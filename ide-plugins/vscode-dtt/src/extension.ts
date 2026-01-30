import * as vscode from 'vscode';
import { DTTDiagnosticProvider } from './dttDiagnostics';
import { DTTTaskProvider } from './dttTaskProvider';

let diagnosticProvider: DTTDiagnosticProvider;
let taskProvider: vscode.Disposable;
let outputChannel: vscode.OutputChannel;
let statusBarItem: vscode.StatusBarItem;

export function activate(context: vscode.ExtensionContext) {
    outputChannel = vscode.window.createOutputChannel('DTT');
    outputChannel.appendLine('DTT extension activated');

    // Create status bar item
    statusBarItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 100);
    statusBarItem.command = 'dtt.showOutput';
    context.subscriptions.push(statusBarItem);

    // Create diagnostic provider
    diagnosticProvider = new DTTDiagnosticProvider(outputChannel, statusBarItem);
    context.subscriptions.push(diagnosticProvider);

    // Register task provider
    taskProvider = vscode.tasks.registerTaskProvider('dtt', new DTTTaskProvider());
    context.subscriptions.push(taskProvider);

    // Register commands
    context.subscriptions.push(
        vscode.commands.registerCommand('dtt.analyzeFile', () => {
            const editor = vscode.window.activeTextEditor;
            if (editor && isCppFile(editor.document)) {
                diagnosticProvider.analyzeFile(editor.document);
            } else {
                vscode.window.showWarningMessage('DTT: No C++ file is currently open');
            }
        })
    );

    context.subscriptions.push(
        vscode.commands.registerCommand('dtt.analyzeWorkspace', async () => {
            const files = await vscode.workspace.findFiles('**/*.{cpp,cc,cxx,c++,C}', '**/build/**');
            if (files.length === 0) {
                vscode.window.showInformationMessage('DTT: No C++ files found in workspace');
                return;
            }

            const progressOptions: vscode.ProgressOptions = {
                location: vscode.ProgressLocation.Notification,
                title: 'DTT Analysis',
                cancellable: true
            };

            await vscode.window.withProgress(progressOptions, async (progress, token) => {
                for (let i = 0; i < files.length; i++) {
                    if (token.isCancellationRequested) {
                        break;
                    }
                    progress.report({
                        message: `Analyzing ${files[i].fsPath} (${i + 1}/${files.length})`,
                        increment: (100 / files.length)
                    });
                    const doc = await vscode.workspace.openTextDocument(files[i]);
                    await diagnosticProvider.analyzeFile(doc);
                }
            });
        })
    );

    context.subscriptions.push(
        vscode.commands.registerCommand('dtt.clearDiagnostics', () => {
            diagnosticProvider.clearAllDiagnostics();
            vscode.window.showInformationMessage('DTT: Diagnostics cleared');
        })
    );

    context.subscriptions.push(
        vscode.commands.registerCommand('dtt.showOutput', () => {
            outputChannel.show();
        })
    );

    context.subscriptions.push(
        vscode.commands.registerCommand('dtt.setMode', async () => {
            const modes = [
                { label: 'inform', description: 'Default: warnings for gaps, errors for bugs' },
                { label: 'enforce', description: 'Strict: errors for everything' },
                { label: 'quiet', description: 'Permissive: errors only for proven bugs' }
            ];
            const selected = await vscode.window.showQuickPick(modes, {
                placeHolder: 'Select DTT analysis mode'
            });
            if (selected) {
                const config = vscode.workspace.getConfiguration('dtt');
                await config.update('mode', selected.label, vscode.ConfigurationTarget.Workspace);
                vscode.window.showInformationMessage(`DTT: Mode set to ${selected.label}`);
            }
        })
    );

    // Auto-analyze on save
    context.subscriptions.push(
        vscode.workspace.onDidSaveTextDocument((document) => {
            const config = vscode.workspace.getConfiguration('dtt');
            if (config.get<boolean>('analyzeOnSave') && isCppFile(document)) {
                diagnosticProvider.analyzeFile(document);
            }
        })
    );

    // Auto-analyze on open
    context.subscriptions.push(
        vscode.workspace.onDidOpenTextDocument((document) => {
            const config = vscode.workspace.getConfiguration('dtt');
            if (config.get<boolean>('analyzeOnOpen') && isCppFile(document)) {
                diagnosticProvider.analyzeFile(document);
            }
        })
    );

    // Update status bar on active editor change
    context.subscriptions.push(
        vscode.window.onDidChangeActiveTextEditor((editor) => {
            if (editor && isCppFile(editor.document)) {
                diagnosticProvider.updateStatusBar(editor.document.uri);
            } else {
                statusBarItem.hide();
            }
        })
    );

    outputChannel.appendLine('DTT extension ready');
}

function isCppFile(document: vscode.TextDocument): boolean {
    return document.languageId === 'cpp' ||
           document.languageId === 'c' ||
           /\.(cpp|cc|cxx|c\+\+|C|c|h|hpp|hxx)$/.test(document.fileName);
}

export function deactivate() {
    if (diagnosticProvider) {
        diagnosticProvider.dispose();
    }
    if (outputChannel) {
        outputChannel.dispose();
    }
}
