/**
 * DTT VS Code Extension
 * Thin integration layer - delegates to parser, runner, and setup
 */

import * as vscode from 'vscode';
import * as path from 'path';
import { parse, Diagnostic } from './parser';
import { Runner, RunConfig } from './runner';
import { ensureBinaries } from './setup';

const runner = new Runner();
const diagnostics = vscode.languages.createDiagnosticCollection('dtt');
const debounceTimers = new Map<string, NodeJS.Timeout>();
let isReady = false;
let setupInProgress = false;
let setupPromise: Promise<void> | null = null;

export async function activate(ctx: vscode.ExtensionContext): Promise<void> {
  ctx.subscriptions.push(
    diagnostics,
    vscode.commands.registerCommand('dtt.analyze', cmdAnalyze),
    vscode.commands.registerCommand('dtt.clear', cmdClear),
    vscode.commands.registerCommand('dtt.setup', () => setup(ctx)),
    vscode.workspace.onDidSaveTextDocument(onSave),
    vscode.workspace.onDidChangeTextDocument(onChange),
    vscode.workspace.onDidCloseTextDocument(onClose)
  );

  // Auto-setup on first C++ file open - with race condition protection
  ctx.subscriptions.push(
    vscode.workspace.onDidOpenTextDocument(async (doc) => {
      if (!isReady && !setupInProgress && isCpp(doc)) {
        try {
          await setup(ctx);
        } catch (err) {
          // Log error but don't crash - user can manually retry with dtt.setup command
          console.error('DTT: Auto-setup failed:', err);
        }
      }
    })
  );

  // Check if already configured
  const cfg = vscode.workspace.getConfiguration('dtt');
  if (cfg.get<string>('pluginPath')) {
    isReady = true;
  }
}

export function deactivate(): void {
  // Clear all pending debounce timers
  for (const timer of debounceTimers.values()) {
    clearTimeout(timer);
  }
  debounceTimers.clear();

  runner.cancelAll();
}

// --- Setup ---

async function setup(ctx: vscode.ExtensionContext): Promise<void> {
  // Prevent multiple concurrent setup calls
  if (setupInProgress && setupPromise) {
    await setupPromise;
    return;
  }

  setupInProgress = true;
  setupPromise = (async () => {
    try {
      const binaries = await ensureBinaries(ctx);
      isReady = binaries !== null;
    } finally {
      setupInProgress = false;
      setupPromise = null;
    }
  })();

  await setupPromise;
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
  if (isReady && getConfig().get<boolean>('analyzeOnSave') && isCpp(doc)) {
    analyze(doc).catch(err => {
      console.error('DTT: Analysis failed:', err);
    });
  }
}

function onChange(e: vscode.TextDocumentChangeEvent): void {
  if (!isReady || !getConfig().get<boolean>('analyzeOnChange') || !isCpp(e.document)) return;

  const uri = e.document.uri.toString();
  const delay = getConfig().get<number>('debounceMs') ?? 500;

  const existing = debounceTimers.get(uri);
  if (existing) clearTimeout(existing);

  debounceTimers.set(uri, setTimeout(() => {
    debounceTimers.delete(uri);
    analyze(e.document);
  }, delay));
}

function onClose(doc: vscode.TextDocument): void {
  // Clear pending debounce timer for this document
  const uri = doc.uri.toString();
  const timer = debounceTimers.get(uri);
  if (timer) {
    clearTimeout(timer);
    debounceTimers.delete(uri);
  }

  diagnostics.delete(doc.uri);
  runner.cancel(doc.uri.fsPath);
}

// --- Core Analysis ---

async function analyze(doc: vscode.TextDocument): Promise<void> {
  const config = getRunConfig(doc);
  if (!config) return;

  const filePath = doc.uri.fsPath;
  const version = doc.version;
  const result = await runner.run(filePath, config);
  if (result.status === 'cancelled' || doc.isClosed || doc.version !== version) return;
  const parsed = parse(result.output, path.basename(filePath));

  const compilerError = result.output.split(/\r?\n/).some(line =>
    /\b(?:fatal )?error:/.test(line) && !line.includes('DTT:'));
  if (result.status === 'failed' || compilerError ||
      (result.exitCode !== 0 && !parsed.some(d => d.severity === 'error'))) {
    const detail = result.output.trim().split(/\r?\n/)[0] || `compiler exited with code ${result.exitCode}`;
    const failure = new vscode.Diagnostic(new vscode.Range(0, 0, 0, 0),
      `DTT analysis failed: ${detail}`, vscode.DiagnosticSeverity.Warning);
    failure.source = 'DTT';
    diagnostics.set(doc.uri, [failure]);
    return;
  }

  diagnostics.set(doc.uri, parsed.map(d => toDiagnostic(d, doc)));
}

// --- Helpers ---

function getConfig(): vscode.WorkspaceConfiguration {
  return vscode.workspace.getConfiguration('dtt');
}

function getRunConfig(doc: vscode.TextDocument): RunConfig | null {
  const cfg = vscode.workspace.getConfiguration('dtt', doc.uri);
  const pluginPath = cfg.get<string>('pluginPath');
  const clangPath = cfg.get<string>('clangPath');

  if (!pluginPath || !clangPath) {
    return null;
  }

  const workspaceDir = vscode.workspace.getWorkspaceFolder(doc.uri)?.uri.fsPath ?? path.dirname(doc.uri.fsPath);
  const workingDirectory = cfg.get<string>('workingDirectory') || '.';
  return {
    clangPath,
    pluginPath,
    standard: cfg.get<string>('standard') ?? 'c++20',
    mode: cfg.get<'inform' | 'enforce' | 'quiet'>('mode') ?? 'inform',
    extraArgs: cfg.get<string[]>('extraArgs') ?? [],
    cwd: path.resolve(workspaceDir, workingDirectory)
  };
}

function isCpp(doc: vscode.TextDocument): boolean {
  return doc.languageId === 'cpp' || doc.languageId === 'c';
}

function toDiagnostic(d: Diagnostic, doc: vscode.TextDocument): vscode.Diagnostic {
  const line = Math.max(0, d.line - 1);
  const col = Math.max(0, d.column - 1);

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
