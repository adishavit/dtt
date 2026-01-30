/**
 * DTT Diagnostic Parser
 * Pure functions - no IDE dependencies
 */

export interface Diagnostic {
  file: string;
  line: number;
  column: number;
  endColumn?: number;
  severity: 'error' | 'warning' | 'note';
  message: string;
  source: string;
}

/** Clang diagnostic format: file:line:col: severity: message */
const DIAG_PATTERN = /^(.+?):(\d+):(\d+):\s*(error|warning|note):\s*(.+)$/;

/** Parse clang stderr into structured diagnostics */
export function parse(output: string, filterFile?: string): Diagnostic[] {
  const results: Diagnostic[] = [];

  for (const line of output.split('\n')) {
    const match = line.match(DIAG_PATTERN);
    if (!match) continue;

    const [, file, lineStr, colStr, severity, message] = match;

    // Only include DTT diagnostics
    if (!message.includes('DTT:')) continue;

    // Filter to specific file if requested
    if (filterFile && !file.endsWith(filterFile) && file !== filterFile) continue;

    results.push({
      file,
      line: parseInt(lineStr, 10),
      column: parseInt(colStr, 10),
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
