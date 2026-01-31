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

const TIMEOUT_MS = 30000; // 30 second timeout

/** Manages DTT analysis runs with cancellation support */
export class Runner extends EventEmitter {
  private processes = new Map<string, ChildProcess>();
  private timeouts = new Map<string, NodeJS.Timeout>();

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

      // Set timeout
      const timeout = setTimeout(() => {
        if (this.processes.has(filePath)) {
          proc.kill();
          this.processes.delete(filePath);
          this.timeouts.delete(filePath);
          resolve({ exitCode: -1, output: `DTT: Analysis timed out after ${TIMEOUT_MS / 1000}s` });
        }
      }, TIMEOUT_MS);
      this.timeouts.set(filePath, timeout);

      let output = '';
      proc.stdout?.on('data', (d) => { output += d; });
      proc.stderr?.on('data', (d) => { output += d; });

      proc.on('error', (err) => {
        clearTimeout(this.timeouts.get(filePath));
        this.timeouts.delete(filePath);
        this.processes.delete(filePath);
        resolve({ exitCode: -1, output: `DTT: ${err.message}` });
      });

      proc.on('close', (code) => {
        clearTimeout(this.timeouts.get(filePath));
        this.timeouts.delete(filePath);
        this.processes.delete(filePath);
        resolve({ exitCode: code ?? 0, output });
      });
    });
  }

  /** Cancel a running analysis */
  cancel(filePath: string): void {
    const timeout = this.timeouts.get(filePath);
    if (timeout) {
      clearTimeout(timeout);
      this.timeouts.delete(filePath);
    }
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

  private isWindows(): boolean {
    return process.platform === 'win32';
  }

  private buildArgs(filePath: string, config: RunConfig): string[] {
    const args: string[] = [];

    if (this.isWindows()) {
      // Windows requires -Xclang syntax for plugins
      args.push(
        '-Xclang', '-load', '-Xclang', config.pluginPath,
        '-Xclang', '-add-plugin', '-Xclang', 'dtt'
      );
      if (config.mode !== 'inform') {
        args.push('-Xclang', '-plugin-arg-dtt', '-Xclang', `--dtt-mode=${config.mode}`);
      }
    } else {
      // Unix-like systems use -fplugin
      args.push(`-fplugin=${config.pluginPath}`);
      if (config.mode !== 'inform') {
        args.push(`-fplugin-arg-dtt---dtt-mode=${config.mode}`);
      }
    }

    args.push(
      `-std=${config.standard}`,
      '-fsyntax-only',
      '-fno-color-diagnostics'
    );

    if (config.extraArgs) {
      args.push(...config.extraArgs);
    }

    args.push(filePath);
    return args;
  }
}
