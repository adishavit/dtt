const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { setTimeout: sleep } = require('node:timers/promises');
const { Runner } = require('../../ide-plugins/vscode-dtt/out/runner.js');

const root = path.resolve(__dirname, '../..');
const compiler = path.join(__dirname, 'fake_clang.cjs');
const config = { clangPath: compiler, pluginPath: 'unused.so', standard: 'c++20', mode: 'inform' };

async function ready(file) {
  for (let i = 0; i < 200; ++i) {
    if (fs.existsSync(file)) return Number(fs.readFileSync(file, 'utf8'));
    await sleep(10);
  }
  throw new Error('fake compiler did not start');
}

async function bounded(promise) {
  let timer;
  try {
    return await Promise.race([promise, new Promise((_, reject) => {
      timer = setTimeout(() => reject(new Error('runner did not terminate the compiler')), 2500);
    })]);
  } finally { clearTimeout(timer); }
}

function fixture(t) {
  const directory = fs.mkdtempSync(path.join(root, 'build/runner-'));
  const source = path.join(directory, 'input.cpp');
  const executable = path.join(directory, 'fake clang');
  fs.copyFileSync(compiler, executable);
  fs.chmodSync(executable, 0o755);
  const markers = [];
  t.after(() => {
    for (const marker of markers) {
      if (fs.existsSync(marker)) {
        try { process.kill(Number(fs.readFileSync(marker, 'utf8')), 'SIGKILL'); } catch {}
      }
    }
    fs.rmSync(directory, { recursive: true, force: true });
  });
  return { source, config: { ...config, clangPath: executable }, write(options) {
    const marker = path.join(directory, `ready-${markers.length}`);
    markers.push(marker);
    fs.writeFileSync(source, JSON.stringify({ ...options, ready: marker }));
    return marker;
  } };
}

test('closing the old run cannot untrack its replacement', async t => {
  const f = fixture(t);
  const runner = new Runner();
  t.after(() => runner.cancelAll());
  let marker = f.write({});
  const old = runner.run(f.source, f.config);
  await ready(marker);
  marker = f.write({});
  const replacement = runner.run(f.source, f.config);
  await ready(marker);
  await bounded(old);
  runner.cancel(f.source);
  const result = await bounded(replacement);
  assert.equal(result.status, 'cancelled');
});

test('cancellation escalates when the compiler ignores SIGTERM', async t => {
  const f = fixture(t);
  const runner = new Runner();
  t.after(() => runner.cancelAll());
  const marker = f.write({ ignoreTerm: true });
  const run = runner.run(f.source, f.config);
  await ready(marker);
  runner.cancel(f.source);
  assert.equal((await bounded(run)).status, 'cancelled');
});

test('timeout escalates and reports failed analysis', async t => {
  const f = fixture(t);
  const runner = new Runner(400);
  t.after(() => runner.cancelAll());
  const marker = f.write({ ignoreTerm: true });
  const run = runner.run(f.source, f.config);
  await ready(marker);
  const result = await bounded(run);
  assert.equal(result.status, 'failed');
  assert.match(result.output, /timed out/);
});

test('a completed DTT diagnostic is not an infrastructure failure', async t => {
  const f = fixture(t);
  const runner = new Runner();
  f.write({ delay: 0, exitCode: 1, output: 'input.cpp:1:1: error: DTT: bad access' });
  const result = await bounded(runner.run(f.source, f.config));
  assert.equal(result.status, 'completed');
  assert.equal(result.exitCode, 1);
  assert.match(result.output, /DTT: bad access/);
});

test('project directory and arguments reach the compiler unchanged', async t => {
  const f = fixture(t);
  const runner = new Runner();
  f.write({ delay: 0, echoContext: true });
  const cwd = path.dirname(f.source);
  const extraArgs = ['-Iinclude dir', '-DNAME=a b', '-include', 'config.h'];
  const result = await runner.run(f.source, { ...f.config, cwd, extraArgs });
  assert.equal(result.exitCode, 0);
  const context = JSON.parse(result.output);
  assert.equal(context.cwd, cwd);
  assert.deepEqual(context.args.slice(-5, -1), extraArgs);
});

for (const unsafe of [true, false]) {
  test(`unsaved snapshot overrides disk and preserves local includes (${unsafe})`, async t => {
    const f = fixture(t);
    const dir = path.dirname(f.source);
    fs.writeFileSync(path.join(dir, 'local.h'), '#include <vector>');
    const source = bad => '#include "local.h"\nvoid f(){std::vector<int> v' + (bad ? '' : '{1}') + '; v.front();}';
    const saved = source(!unsafe);
    fs.writeFileSync(f.source, saved);
    const runner = new Runner();
    const result = await runner.run(f.source, {clangPath: 'clang++-18',
      pluginPath: path.join(root, 'build/libDTTPlugin.so'), standard: 'c++20', mode: 'enforce',
      content: source(unsafe)});
    assert.equal(result.exitCode, unsafe ? 1 : 0, result.output);
    if (unsafe) assert.ok(result.output.includes(f.source + ':2:'));
    assert.equal(fs.readFileSync(f.source, 'utf8'), saved);
  });
}

test('snapshot files are removed after compiler completion', async t => {
  const f = fixture(t);
  f.write({delay: 0, echoContext: true});
  const result = await new Runner().run(f.source, {...f.config, content: 'unsaved'});
  const args = JSON.parse(result.output).args;
  const overlay = args[args.indexOf('-ivfsoverlay') + 1];
  assert.equal(fs.existsSync(path.dirname(overlay)), false);
});
