/**
 * DTT Diagnostic Parser
 * Pure functions - no IDE dependencies
 */

import * as path from 'path';

export interface Diagnostic {
  file: string;
  line: number;
  column: number;
  endColumn?: number;
  severity: 'error' | 'warning' | 'note';
  message: string;
  source: string;
}

/**
 * Clang diagnostic format: file:line:col: severity: message
 * Uses [^:]+ instead of .+? to avoid ReDoS vulnerability on pathological inputs
 */
const DIAG_PATTERN = /^([^:]+(?::[^:]+)*):(\d+):(\d+):\s*(error|warning|note):\s*(.+)$/;

/** Maximum safe integer value for line/column numbers */
const MAX_LINE_COL = 10000000;

/** Parse clang stderr into structured diagnostics */
export function parse(output: string, filterFile?: string): Diagnostic[] {
  const results: Diagnostic[] = [];

  // Handle both Unix (\n) and Windows (\r\n) line endings
  for (const line of output.split(/\r?\n/)) {
    const match = line.match(DIAG_PATTERN);
    if (!match) continue;

    const [, file, lineStr, colStr, severity, message] = match;

    // Validate line and column numbers
    const lineNum = parseInt(lineStr, 10);
    const colNum = parseInt(colStr, 10);
    if (!Number.isFinite(lineNum) || !Number.isFinite(colNum) ||
        lineNum < 1 || lineNum > MAX_LINE_COL ||
        colNum < 1 || colNum > MAX_LINE_COL) {
      continue;
    }

    // Only include DTT diagnostics
    if (!message.includes('DTT:')) continue;

    // Filter to specific file if requested - use basename comparison for safety
    if (filterFile) {
      const fileBasename = path.basename(file);
      if (fileBasename !== filterFile && file !== filterFile) {
        continue;
      }
    }

    results.push({
      file,
      line: lineNum,
      column: colNum,
      severity: severity as Diagnostic['severity'],
      message,
      source: 'DTT'
    });
  }

  return results;
}

/** Check if diagnostic indicates a proven bug vs coverage gap */
export function isProvenBug(diag: Diagnostic): boolean {
  // DTT proven violations contain "= X" where X is the actual value
  return /=\s*\d+/.test(diag.message) || diag.message.includes('known to be');
}
