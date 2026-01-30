/**
 * DTT Runner
 * Executes clang with DTT plugin
 */

import { spawn, ChildProcess } from 'child_process';
import { EventEmitter } from 'events';

export interface RunConfig {
  clangPath: string;
  pluginPath: string;
  standard: string;
  mode: 'inform' | 'enforce' | 'quiet';
  extraArgs?: string[];
}

export interface RunResult {
  exitCode: number;
  output: string;
}

/** Manages DTT analysis runs with cancellation support */
export class Runner extends EventEmitter {
  private processes = new Map<string, ChildProcess>();

  /** Run DTT analysis on a file */
  async run(filePath: string, config: RunConfig): Promise<RunResult> {
    // Cancel any existing run for this file
    this.cancel(filePath);

    const args = this.buildArgs(filePath, config);

    return new Promise((resolve) => {
      const proc = spawn(config.clangPath, args, {
        stdio: ['ignore', 'pipe', 'pipe']
      });

      this.processes.set(filePath, proc);

      let output = '';
      proc.stdout?.on('data', (d) => { output += d; });
      proc.stderr?.on('data', (d) => { output += d; });

      proc.on('error', (err) => {
        this.processes.delete(filePath);
        resolve({ exitCode: -1, output: err.message });
      });

      proc.on('close', (code) => {
        this.processes.delete(filePath);
        resolve({ exitCode: code ?? 0, output });
      });
    });
  }

  /** Cancel a running analysis */
  cancel(filePath: string): void {
    const proc = this.processes.get(filePath);
    if (proc) {
      proc.kill();
      this.processes.delete(filePath);
    }
  }

  /** Cancel all running analyses */
  cancelAll(): void {
    for (const [path] of this.processes) {
      this.cancel(path);
    }
  }

  private buildArgs(filePath: string, config: RunConfig): string[] {
    const args = [
      `-fplugin=${config.pluginPath}`,
      `-std=${config.standard}`,
      '-fsyntax-only',
      '-fno-color-diagnostics'
    ];

    if (config.mode !== 'inform') {
      args.push(`-fplugin-arg-dtt---dtt-mode=${config.mode}`);
    }

    if (config.extraArgs) {
      args.push(...config.extraArgs);
    }

    args.push(filePath);
    return args;
  }
}
