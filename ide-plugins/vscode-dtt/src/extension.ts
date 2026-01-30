/**
 * DTT VS Code Extension
 * Thin integration layer - delegates to parser and runner
 */

import * as vscode from 'vscode';
import * as path from 'path';
import { parse, Diagnostic } from './parser';
import { Runner, RunConfig } from './runner';

const runner = new Runner();
const diagnostics = vscode.languages.createDiagnosticCollection('dtt');
const debounceTimers = new Map<string, NodeJS.Timeout>();

export function activate(ctx: vscode.ExtensionContext): void {
  ctx.subscriptions.push(
    diagnostics,
    vscode.commands.registerCommand('dtt.analyze', cmdAnalyze),
    vscode.commands.registerCommand('dtt.clear', cmdClear),
    vscode.workspace.onDidSaveTextDocument(onSave),
    vscode.workspace.onDidChangeTextDocument(onChange),
    vscode.workspace.onDidCloseTextDocument(onClose)
  );
}

export function deactivate(): void {
  runner.cancelAll();
}

// --- Commands ---

async function cmdAnalyze(): Promise<void> {
  const editor = vscode.window.activeTextEditor;
  if (editor && isCpp(editor.document)) {
    await analyze(editor.document);
  }
}

function cmdClear(): void {
  diagnostics.clear();
}

// --- Event Handlers ---

function onSave(doc: vscode.TextDocument): void {
  if (getConfig().get<boolean>('analyzeOnSave') && isCpp(doc)) {
    analyze(doc);
  }
}

function onChange(e: vscode.TextDocumentChangeEvent): void {
  if (!getConfig().get<boolean>('analyzeOnChange') || !isCpp(e.document)) return;

  const uri = e.document.uri.toString();
  const delay = getConfig().get<number>('debounceMs') ?? 500;

  // Debounce
  const existing = debounceTimers.get(uri);
  if (existing) clearTimeout(existing);

  debounceTimers.set(uri, setTimeout(() => {
    debounceTimers.delete(uri);
    analyze(e.document);
  }, delay));
}

function onClose(doc: vscode.TextDocument): void {
  diagnostics.delete(doc.uri);
  runner.cancel(doc.uri.fsPath);
}

// --- Core Analysis ---

async function analyze(doc: vscode.TextDocument): Promise<void> {
  const config = getRunConfig();
  if (!config) return;

  const filePath = doc.uri.fsPath;
  const result = await runner.run(filePath, config);
  const parsed = parse(result.output, path.basename(filePath));

  diagnostics.set(doc.uri, parsed.map(d => toDiagnostic(d, doc)));
}

// --- Helpers ---

function getConfig(): vscode.WorkspaceConfiguration {
  return vscode.workspace.getConfiguration('dtt');
}

function getRunConfig(): RunConfig | null {
  const cfg = getConfig();
  const pluginPath = cfg.get<string>('pluginPath');

  if (!pluginPath) {
    vscode.window.showErrorMessage('DTT: Set dtt.pluginPath in settings');
    return null;
  }

  return {
    clangPath: cfg.get<string>('clangPath') ?? 'clang++',
    pluginPath,
    standard: cfg.get<string>('standard') ?? 'c++20',
    mode: cfg.get<'inform' | 'enforce' | 'quiet'>('mode') ?? 'inform'
  };
}

function isCpp(doc: vscode.TextDocument): boolean {
  return doc.languageId === 'cpp' || doc.languageId === 'c';
}

function toDiagnostic(d: Diagnostic, doc: vscode.TextDocument): vscode.Diagnostic {
  const line = Math.max(0, d.line - 1);
  const col = Math.max(0, d.column - 1);

  // Try to get the word at the position for better range
  const wordRange = doc.getWordRangeAtPosition(new vscode.Position(line, col));
  const range = wordRange ?? new vscode.Range(line, col, line, col + 1);

  const diag = new vscode.Diagnostic(range, d.message, toSeverity(d.severity));
  diag.source = 'DTT';
  return diag;
}

function toSeverity(s: Diagnostic['severity']): vscode.DiagnosticSeverity {
  switch (s) {
    case 'error': return vscode.DiagnosticSeverity.Error;
    case 'warning': return vscode.DiagnosticSeverity.Warning;
    case 'note': return vscode.DiagnosticSeverity.Information;
  }
}
