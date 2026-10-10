/**
 * DTT Runner
 * Executes clang with DTT plugin
 */

import { spawn } from 'child_process';
import { EventEmitter } from 'events';

export interface RunConfig {
  clangPath: string;
  pluginPath: string;
  standard: string;
  mode: 'inform' | 'enforce' | 'quiet';
  extraArgs?: string[];
  cwd?: string;
}

export interface RunResult {
  exitCode: number;
  output: string;
  status: 'completed' | 'cancelled' | 'failed';
}

const TIMEOUT_MS = 30000; // 30 second timeout
const MAX_OUTPUT_SIZE = 5 * 1024 * 1024; // 5MB max output buffer

/** Manages DTT analysis runs with cancellation support */
export class Runner extends EventEmitter {
  private processes = new Map<string, { cancel: () => void }>();

  constructor(private readonly timeoutMs = TIMEOUT_MS) {
    super();
  }

  /** Run DTT analysis on a file */
  async run(filePath: string, config: RunConfig): Promise<RunResult> {
    // Cancel any existing run for this file
    this.cancel(filePath);

    const args = this.buildArgs(filePath, config);

    return new Promise((resolve) => {
      let resolved = false;
      let exited = false;
      let timeout: NodeJS.Timeout | undefined;
      let escalation: NodeJS.Timeout | undefined;
      let stopStatus: 'cancelled' | 'failed' | undefined;
      let stopMessage = '';
      const run = { cancel: () => stop('cancelled', '') };
      const doResolve = (result: RunResult) => {
        if (!resolved) {
          resolved = true;
          clearTimeout(timeout);
          clearTimeout(escalation);
          // A superseded process owns only its own timers and registry entry.
          if (this.processes.get(filePath) === run) this.processes.delete(filePath);
          resolve(result);
        }
      };

      const proc = spawn(config.clangPath, args, {
        cwd: config.cwd,
        stdio: ['ignore', 'pipe', 'pipe']
      });

      this.processes.set(filePath, run);

      // Sending SIGTERM does not prove exit. Escalation belongs to this process,
      // even after a replacement has occupied its filename in the registry.
      const stop = (status: 'cancelled' | 'failed', message: string) => {
        if (exited || resolved || stopStatus) return;
        stopStatus = status;
        stopMessage = message;
        clearTimeout(timeout);
        proc.kill('SIGTERM');
        escalation = setTimeout(() => {
          if (!exited && proc.exitCode === null && proc.signalCode === null) proc.kill('SIGKILL');
        }, 1000);
      };
      timeout = setTimeout(() => {
        stop('failed', `DTT: Analysis timed out after ${this.timeoutMs / 1000}s`);
      }, this.timeoutMs);

      let output = '';
      let outputTruncated = false;

      const appendOutput = (chunk: Buffer | string) => {
        if (outputTruncated) return;
        const str = chunk.toString();
        if (output.length + str.length > MAX_OUTPUT_SIZE) {
          output += str.slice(0, MAX_OUTPUT_SIZE - output.length);
          output += '\n[DTT: Output truncated - exceeded 5MB limit]';
          outputTruncated = true;
          // Kill process if output is too large
          stop('failed', 'DTT: Analysis output exceeded 5MB limit');
        } else {
          output += str;
        }
      };

      proc.stdout?.on('data', appendOutput);
      proc.stderr?.on('data', appendOutput);

      proc.on('error', (err) => {
        doResolve({ exitCode: -1, output: `DTT: ${err.message}`, status: 'failed' });
      });

      proc.on('close', (code) => {
        exited = true;
        doResolve({
          exitCode: code ?? -1,
          output: stopMessage ? `${stopMessage}\n${output}` : output,
          status: stopStatus ?? (code === null ? 'failed' : 'completed')
        });
      });
    });
  }

  /** Cancel a running analysis with SIGKILL escalation */
  cancel(filePath: string): void {
    this.processes.get(filePath)?.cancel();
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
