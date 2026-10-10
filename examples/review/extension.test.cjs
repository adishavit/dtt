const { test } = require('node:test');
const assert = require('node:assert/strict');
const Module = require('node:module');

async function fixture(run, settings = {}, workspacePath = "/project") {
  const commands = new Map(), writes = [];
  const doc = {
    uri: { fsPath: '/project/test.cpp', toString: () => 'file:///project/test.cpp' },
    languageId: 'cpp', version: 1, isClosed: false,
    getText: () => 'unsaved buffer text',
    getWordRangeAtPosition: () => undefined
  };
  const vscode = {
    languages: { createDiagnosticCollection: () => ({
      set: (uri, diagnostics) => writes.push(diagnostics), delete() {}, clear() {}
    }) },
    commands: { registerCommand: (name, callback) => commands.set(name, callback) },
    workspace: {
      getConfiguration: () => ({ get: name => ({ pluginPath: 'plugin', clangPath: 'clang', ...settings })[name] }),
      getWorkspaceFolder: () => workspacePath ? { uri: { fsPath: workspacePath } } : undefined,
      onDidSaveTextDocument() {}, onDidChangeTextDocument() {},
      onDidCloseTextDocument() {}, onDidOpenTextDocument() {}
    },
    window: { activeTextEditor: { document: doc } },
    Position: class {}, Range: class {},
    Diagnostic: class { constructor(range, message, severity) { Object.assign(this, { range, message, severity }); } },
    DiagnosticSeverity: { Error: 0, Warning: 1, Information: 2, Hint: 3 }
  };
  const original = Module._load;
  Module._load = function(request, parent, ...rest) {
    if (request === 'vscode') return vscode;
    if (parent?.filename.endsWith('/out/extension.js')) {
      if (request === './runner') return { Runner: class { run(file, config) { return run(doc, config); } cancelAll() {} } };
      if (request === './setup') return { ensureBinaries: async () => null };
    }
    return original.call(this, request, parent, ...rest);
  };
  const location = require.resolve('../../ide-plugins/vscode-dtt/out/extension.js');
  delete require.cache[location];
  let extension;
  try { extension = require(location); } finally { Module._load = original; }
  await extension.activate({ subscriptions: [] });
  await commands.get('dtt.analyze')();
  extension.deactivate();
  return writes;
}

test('cancelled analysis cannot clear diagnostics', async () => {
  const writes = await fixture(async () => ({ status: 'cancelled', exitCode: -1, output: '' }));
  assert.equal(writes.length, 0);
});

test('results for an older document version are discarded', async () => {
  const writes = await fixture(async doc => {
    doc.version++;
    return { status: 'completed', exitCode: 0, output: '' };
  });
  assert.equal(writes.length, 0);
});

for (const result of [
  { status: 'failed', exitCode: -1, output: 'DTT: compiler not found' },
  { status: 'completed', exitCode: 1, output: 'test.cpp:1:1: fatal error: header not found' },
  { status: 'completed', exitCode: 1, output: '' }
]) {
  test(`failed analysis stays visible: ${result.output || 'no output'}`, async () => {
    const writes = await fixture(async () => result);
    assert.equal(writes.length, 1);
    assert.equal(writes[0].length, 1);
    assert.match(writes[0][0].message, /analysis failed/i);
  });
}

test('a successful clean analysis clears diagnostics', async () => {
  const writes = await fixture(async () => ({ status: 'completed', exitCode: 0, output: '' }));
  assert.deepEqual(writes, [[]]);
});

test('a DTT error remains an ordinary analysis result', async () => {
  const writes = await fixture(async () => ({
    status: 'completed', exitCode: 1, output: '/project/test.cpp:1:1: error: DTT: bad access'
  }));
  assert.equal(writes[0][0].severity, 0);
  assert.doesNotMatch(writes[0][0].message, /analysis failed/i);
});

for (const [directory, workspace, expected] of [
  ['', '/project', '/project'],
  ['build dir', '/project', '/project/build dir'],
  ['/custom', '/project', '/custom'],
  ['', null, '/project']
]) {
  test(`project settings reach analysis: ${directory || 'default'}`, async () => {
    const extraArgs = ['-Iinclude dir', '-DFEATURE=1', '-include', 'config.h'];
    await fixture(async (doc, config) => {
      assert.deepEqual(config.extraArgs, extraArgs);
      assert.equal(config.cwd, expected);
      return { status: 'completed', exitCode: 0, output: '' };
    }, { extraArgs, workingDirectory: directory }, workspace);
  });
}

test('analysis receives the current unsaved document snapshot', async () => {
  await fixture(async (doc, config) => {
    assert.equal(config.content, doc.getText());
    return { status: 'completed', exitCode: 0, output: '' };
  });
});
