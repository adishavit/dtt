import * as vscode from 'vscode';
import * as cp from 'child_process';
import * as path from 'path';

export interface DTTDiagnostic {
    file: string;
    line: number;
    column: number;
    severity: 'error' | 'warning' | 'note';
    message: string;
    category?: string;  // DTT category (C, D, E, F, G)
}

/**
 * DTT Diagnostic Categories:
 * - E/F: Proven violation (definite bug)
 * - C: Lost tracking (used to know state but lost it)
 * - D: Undecidable (solver couldn't verify)
 * - G: Annotation conflict
 */
export class DTTDiagnosticProvider implements vscode.Disposable {
    private diagnosticCollection: vscode.DiagnosticCollection;
    private outputChannel: vscode.OutputChannel;
    private statusBarItem: vscode.StatusBarItem;
    private runningProcesses: Map<string, cp.ChildProcess> = new Map();
    private diagnosticCounts: Map<string, { errors: number; warnings: number }> = new Map();

    constructor(outputChannel: vscode.OutputChannel, statusBarItem: vscode.StatusBarItem) {
        this.diagnosticCollection = vscode.languages.createDiagnosticCollection('dtt');
        this.outputChannel = outputChannel;
        this.statusBarItem = statusBarItem;
    }

    async analyzeFile(document: vscode.TextDocument): Promise<void> {
        const filePath = document.uri.fsPath;

        // Cancel any existing analysis for this file
        const existingProcess = this.runningProcesses.get(filePath);
        if (existingProcess) {
            existingProcess.kill();
            this.runningProcesses.delete(filePath);
        }

        const config = vscode.workspace.getConfiguration('dtt');
        const pluginPath = config.get<string>('pluginPath');

        if (!pluginPath) {
            vscode.window.showErrorMessage(
                'DTT: Plugin path not configured. Set dtt.pluginPath in settings.',
                'Open Settings'
            ).then(selection => {
                if (selection === 'Open Settings') {
                    vscode.commands.executeCommand('workbench.action.openSettings', 'dtt.pluginPath');
                }
            });
            return;
        }

        const clangPath = config.get<string>('clangPath') || 'clang++';
        const mode = config.get<string>('mode') || 'inform';
        const cppStandard = config.get<string>('cppStandard') || 'c++20';
        const additionalFlags = config.get<string[]>('additionalFlags') || [];
        const includePaths = config.get<string[]>('includePaths') || [];

        // Build command arguments
        const args: string[] = [
            `-fplugin=${pluginPath}`,
            `-std=${cppStandard}`,
            '-fsyntax-only',
            '-fno-color-diagnostics'
        ];

        // Add mode flag if not inform (default)
        if (mode !== 'inform') {
            args.push(`-fplugin-arg-dtt---dtt-mode=${mode}`);
        }

        // Add include paths
        for (const includePath of includePaths) {
            args.push(`-I${includePath}`);
        }

        // Add workspace root as include path
        const workspaceFolder = vscode.workspace.getWorkspaceFolder(document.uri);
        if (workspaceFolder) {
            args.push(`-I${workspaceFolder.uri.fsPath}`);
        }

        // Add additional flags
        args.push(...additionalFlags);

        // Add the file to analyze
        args.push(filePath);

        this.outputChannel.appendLine(`\n[DTT] Analyzing: ${filePath}`);
        this.outputChannel.appendLine(`[DTT] Command: ${clangPath} ${args.join(' ')}`);

        // Update status bar
        this.statusBarItem.text = '$(sync~spin) DTT analyzing...';
        this.statusBarItem.show();

        return new Promise<void>((resolve) => {
            const process = cp.spawn(clangPath, args, {
                cwd: workspaceFolder?.uri.fsPath
            });

            this.runningProcesses.set(filePath, process);

            let stdout = '';
            let stderr = '';

            process.stdout?.on('data', (data) => {
                stdout += data.toString();
            });

            process.stderr?.on('data', (data) => {
                stderr += data.toString();
            });

            process.on('error', (error) => {
                this.outputChannel.appendLine(`[DTT] Error: ${error.message}`);
                this.statusBarItem.text = '$(error) DTT error';
                this.runningProcesses.delete(filePath);
                resolve();
            });

            process.on('close', (code) => {
                this.runningProcesses.delete(filePath);

                const output = stdout + stderr;
                this.outputChannel.appendLine(output);

                const diagnostics = this.parseDiagnostics(output, filePath);
                this.setDiagnostics(document.uri, diagnostics);

                const errorCount = diagnostics.filter(d => d.severity === vscode.DiagnosticSeverity.Error).length;
                const warningCount = diagnostics.filter(d => d.severity === vscode.DiagnosticSeverity.Warning).length;

                this.diagnosticCounts.set(filePath, { errors: errorCount, warnings: warningCount });
                this.updateStatusBar(document.uri);

                this.outputChannel.appendLine(`[DTT] Analysis complete: ${errorCount} errors, ${warningCount} warnings`);
                resolve();
            });
        });
    }

    private parseDiagnostics(output: string, currentFile: string): vscode.Diagnostic[] {
        const diagnostics: vscode.Diagnostic[] = [];
        const lines = output.split('\n');

        // Match clang diagnostic format: file:line:column: severity: message
        // DTT messages contain "DTT:" prefix
        const diagnosticRegex = /^(.+?):(\d+):(\d+):\s+(error|warning|note):\s+(.+)$/;

        for (let i = 0; i < lines.length; i++) {
            const line = lines[i];
            const match = line.match(diagnosticRegex);

            if (match) {
                const [, file, lineNum, colNum, severityStr, message] = match;

                // Only include diagnostics for the current file
                const normalizedFile = path.normalize(file);
                const normalizedCurrentFile = path.normalize(currentFile);

                if (normalizedFile !== normalizedCurrentFile &&
                    !normalizedFile.endsWith(path.basename(normalizedCurrentFile))) {
                    continue;
                }

                // Skip non-DTT diagnostics unless they're clearly related
                if (!message.includes('DTT:') && !message.includes('dtt:')) {
                    // Include notes that follow DTT diagnostics
                    if (severityStr !== 'note') {
                        continue;
                    }
                }

                const lineIndex = parseInt(lineNum, 10) - 1;
                const colIndex = parseInt(colNum, 10) - 1;

                const severity = this.mapSeverity(severityStr);
                const range = new vscode.Range(
                    new vscode.Position(Math.max(0, lineIndex), Math.max(0, colIndex)),
                    new vscode.Position(Math.max(0, lineIndex), Math.max(0, colIndex) + 1)
                );

                const diagnostic = new vscode.Diagnostic(range, message, severity);
                diagnostic.source = 'DTT';

                // Extract DTT category if present
                const categoryMatch = message.match(/\[Category ([A-G])\]/);
                if (categoryMatch) {
                    diagnostic.code = `Category ${categoryMatch[1]}`;
                }

                // Add related information for multi-line diagnostics
                const relatedInfo: vscode.DiagnosticRelatedInformation[] = [];

                // Look for "note:" lines that follow
                for (let j = i + 1; j < lines.length && j < i + 5; j++) {
                    const noteLine = lines[j];
                    const noteMatch = noteLine.match(diagnosticRegex);
                    if (noteMatch && noteMatch[4] === 'note') {
                        const [, noteFile, noteLine, noteCol, , noteMessage] = noteMatch;
                        const noteUri = vscode.Uri.file(noteFile);
                        const noteLineIndex = parseInt(noteLine, 10) - 1;
                        const noteColIndex = parseInt(noteCol, 10) - 1;
                        const noteRange = new vscode.Range(
                            new vscode.Position(Math.max(0, noteLineIndex), Math.max(0, noteColIndex)),
                            new vscode.Position(Math.max(0, noteLineIndex), Math.max(0, noteColIndex) + 1)
                        );
                        relatedInfo.push(new vscode.DiagnosticRelatedInformation(
                            new vscode.Location(noteUri, noteRange),
                            noteMessage
                        ));
                    } else if (noteMatch) {
                        break;  // Hit another error/warning, stop
                    }
                }

                if (relatedInfo.length > 0) {
                    diagnostic.relatedInformation = relatedInfo;
                }

                diagnostics.push(diagnostic);
            }
        }

        return diagnostics;
    }

    private mapSeverity(severity: string): vscode.DiagnosticSeverity {
        switch (severity.toLowerCase()) {
            case 'error':
                return vscode.DiagnosticSeverity.Error;
            case 'warning':
                return vscode.DiagnosticSeverity.Warning;
            case 'note':
                return vscode.DiagnosticSeverity.Information;
            default:
                return vscode.DiagnosticSeverity.Warning;
        }
    }

    private setDiagnostics(uri: vscode.Uri, diagnostics: vscode.Diagnostic[]): void {
        this.diagnosticCollection.set(uri, diagnostics);
    }

    updateStatusBar(uri: vscode.Uri): void {
        const counts = this.diagnosticCounts.get(uri.fsPath);
        if (counts) {
            if (counts.errors > 0) {
                this.statusBarItem.text = `$(error) DTT: ${counts.errors} error(s)`;
                this.statusBarItem.backgroundColor = new vscode.ThemeColor('statusBarItem.errorBackground');
            } else if (counts.warnings > 0) {
                this.statusBarItem.text = `$(warning) DTT: ${counts.warnings} warning(s)`;
                this.statusBarItem.backgroundColor = new vscode.ThemeColor('statusBarItem.warningBackground');
            } else {
                this.statusBarItem.text = '$(check) DTT: No issues';
                this.statusBarItem.backgroundColor = undefined;
            }
            this.statusBarItem.show();
        } else {
            this.statusBarItem.text = '$(circle-outline) DTT';
            this.statusBarItem.backgroundColor = undefined;
            this.statusBarItem.show();
        }
    }

    clearAllDiagnostics(): void {
        this.diagnosticCollection.clear();
        this.diagnosticCounts.clear();
        this.statusBarItem.text = '$(circle-outline) DTT';
        this.statusBarItem.backgroundColor = undefined;
    }

    dispose(): void {
        // Kill all running processes
        for (const [, process] of this.runningProcesses) {
            process.kill();
        }
        this.runningProcesses.clear();
        this.diagnosticCollection.dispose();
    }
}
