import * as vscode from 'vscode';
import * as path from 'path';

interface DTTTaskDefinition extends vscode.TaskDefinition {
    file: string;
    mode?: 'inform' | 'enforce' | 'quiet';
}

export class DTTTaskProvider implements vscode.TaskProvider {
    static readonly TaskType = 'dtt';

    provideTasks(): vscode.ProviderResult<vscode.Task[]> {
        return this.getTasks();
    }

    resolveTask(task: vscode.Task): vscode.ProviderResult<vscode.Task> {
        const definition = task.definition as DTTTaskDefinition;
        if (definition.file) {
            return this.createTask(definition);
        }
        return undefined;
    }

    private async getTasks(): Promise<vscode.Task[]> {
        const tasks: vscode.Task[] = [];
        const workspaceFolders = vscode.workspace.workspaceFolders;

        if (!workspaceFolders) {
            return tasks;
        }

        // Create a task for the current file if it's a C++ file
        const editor = vscode.window.activeTextEditor;
        if (editor && this.isCppFile(editor.document.fileName)) {
            const definition: DTTTaskDefinition = {
                type: DTTTaskProvider.TaskType,
                file: editor.document.uri.fsPath
            };
            tasks.push(this.createTask(definition));
        }

        return tasks;
    }

    private createTask(definition: DTTTaskDefinition): vscode.Task {
        const config = vscode.workspace.getConfiguration('dtt');
        const pluginPath = config.get<string>('pluginPath') || '';
        const clangPath = config.get<string>('clangPath') || 'clang++';
        const mode = definition.mode || config.get<string>('mode') || 'inform';
        const cppStandard = config.get<string>('cppStandard') || 'c++20';
        const additionalFlags = config.get<string[]>('additionalFlags') || [];
        const includePaths = config.get<string[]>('includePaths') || [];

        // Build the command
        let command = `${clangPath} -fplugin=${pluginPath} -std=${cppStandard} -fsyntax-only`;

        if (mode !== 'inform') {
            command += ` -fplugin-arg-dtt---dtt-mode=${mode}`;
        }

        for (const includePath of includePaths) {
            command += ` -I${includePath}`;
        }

        for (const flag of additionalFlags) {
            command += ` ${flag}`;
        }

        command += ` "${definition.file}"`;

        const fileName = path.basename(definition.file);
        const task = new vscode.Task(
            definition,
            vscode.TaskScope.Workspace,
            `Analyze ${fileName}`,
            'DTT',
            new vscode.ShellExecution(command),
            '$dtt'  // Use the DTT problem matcher
        );

        task.group = vscode.TaskGroup.Build;
        task.presentationOptions = {
            reveal: vscode.TaskRevealKind.Silent,
            panel: vscode.TaskPanelKind.Dedicated,
            clear: true
        };

        return task;
    }

    private isCppFile(fileName: string): boolean {
        return /\.(cpp|cc|cxx|c\+\+|C|c|h|hpp|hxx)$/.test(fileName);
    }
}
