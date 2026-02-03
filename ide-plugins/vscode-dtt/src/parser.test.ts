/**
 * Parser Unit Tests
 * Run with: npx ts-node src/parser.test.ts
 */

import { parse, isProvenBug, Diagnostic } from './parser';
import * as assert from 'assert';

function test(name: string, fn: () => void): void {
  try {
    fn();
    console.log(`\u2713 ${name}`);
  } catch (e) {
    console.error(`\u2717 ${name}`);
    console.error(`  ${e}`);
    process.exitCode = 1;
  }
}

// Sample DTT output
const SAMPLE_OUTPUT = `
/home/user/test.cpp:10:12: error: DTT: potential null pointer dereference of 'ptr' (known to be null)
    return *ptr;
           ^~~~
/home/user/test.cpp:5:5: note: consider adding null check
/home/user/test.cpp:20:8: warning: DTT: buffer index 'i' may be out of bounds (buffer size is 10)
    arr[i] = 0;
       ^
/home/user/other.cpp:30:1: error: DTT: use after move of 'vec'
    vec.push_back(1);
    ^~~
`;

// Regular clang output (should be filtered out)
const NON_DTT_OUTPUT = `
/home/user/test.cpp:1:1: error: unknown type name 'foo'
/home/user/test.cpp:2:1: warning: unused variable 'x'
`;

test('parse: extracts DTT errors', () => {
  const diags = parse(SAMPLE_OUTPUT);
  const errors = diags.filter(d => d.severity === 'error');
  assert.strictEqual(errors.length, 2);
  assert.strictEqual(errors[0].line, 10);
  assert.strictEqual(errors[0].column, 12);
  assert.ok(errors[0].message.includes('null pointer'));
});

test('parse: extracts DTT warnings', () => {
  const diags = parse(SAMPLE_OUTPUT);
  const warnings = diags.filter(d => d.severity === 'warning');
  assert.strictEqual(warnings.length, 1);
  assert.strictEqual(warnings[0].line, 20);
  assert.ok(warnings[0].message.includes('buffer'));
});

test('parse: filters non-DTT diagnostics', () => {
  const diags = parse(NON_DTT_OUTPUT);
  assert.strictEqual(diags.length, 0);
});

test('parse: filters by filename', () => {
  const diags = parse(SAMPLE_OUTPUT, 'test.cpp');
  // Should only include test.cpp diagnostics
  assert.ok(diags.every(d => d.file.endsWith('test.cpp')));
  // Note is from test.cpp so it should be included
  const fromOther = diags.filter(d => d.file.includes('other.cpp'));
  assert.strictEqual(fromOther.length, 0);
});

test('parse: handles empty output', () => {
  const diags = parse('');
  assert.strictEqual(diags.length, 0);
});

test('parse: handles malformed lines gracefully', () => {
  const output = `
not a diagnostic line
/incomplete:line
/home/user/test.cpp:10:12: error: DTT: valid error
random garbage
`;
  const diags = parse(output);
  assert.strictEqual(diags.length, 1);
  assert.strictEqual(diags[0].line, 10);
});

test('isProvenBug: identifies known values', () => {
  const diag: Diagnostic = {
    file: 'test.cpp',
    line: 10,
    column: 5,
    severity: 'error',
    message: "DTT: ptr requires != nullptr, but obj._ptr = 0",
    source: 'DTT'
  };
  assert.strictEqual(isProvenBug(diag), true);
});

test('isProvenBug: identifies known-to-be messages', () => {
  const diag: Diagnostic = {
    file: 'test.cpp',
    line: 10,
    column: 5,
    severity: 'error',
    message: "DTT: potential null pointer dereference of 'ptr' (known to be null)",
    source: 'DTT'
  };
  assert.strictEqual(isProvenBug(diag), true);
});

test('isProvenBug: rejects unknown/gap messages', () => {
  const diag: Diagnostic = {
    file: 'test.cpp',
    line: 10,
    column: 5,
    severity: 'error',
    message: "DTT: ptr requires != nullptr, but obj._ptr is unknown",
    source: 'DTT'
  };
  assert.strictEqual(isProvenBug(diag), false);
});

console.log('\nParser tests completed');
