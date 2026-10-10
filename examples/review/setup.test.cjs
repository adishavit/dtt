const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const Module = require('node:module');
const { PassThrough, Writable } = require('node:stream');
const { EventEmitter } = require('node:events');

const bytes = Buffer.from('complete plugin fixture');
const hash = crypto.createHash('sha256').update(bytes).digest('hex');

function fixture(t, options = {}) {
  const home = fs.mkdtempSync(path.resolve(__dirname, '../../build/setup-'));
  const directory = path.join(home, '.dtt/bin');
  const plugin = path.join(directory, 'libDTTPlugin.so');
  fs.mkdirSync(directory, { recursive: true });
  if (options.existing) fs.writeFileSync(plugin, options.existing);
  t.after(() => fs.rmSync(home, { recursive: true, force: true }));
  const errors = [], updates = [], messages = [];
  const vscode = {
    ProgressLocation: { Notification: 1 }, ConfigurationTarget: { Global: 1 },
    workspace: { getConfiguration: () => ({ get: name => options.settings?.[name], update: async (...args) => updates.push(args) }) },
    window: {
      showWarningMessage() {}, showErrorMessage(message) { errors.push(message); },
      showInformationMessage(message) { messages.push(message); },
      withProgress: async (_, run) => run({ report() {
        if (options.existing) assert.equal(fs.readFileSync(plugin, 'utf8'), options.existing);
        else assert.equal(fs.existsSync(plugin), false, 'unverified download became discoverable');
      } })
    }
  };
  const https = { get(url, _, callback) {
    if (options.llvmAsset) assert.equal(url.split('/').at(-1), options.llvmAsset);
    if (options.noLLVMAsset) assert.fail('unsupported platform attempted a download');
    const request = new EventEmitter();
    request.setTimeout = () => request;
    const response = new PassThrough();
    const checksum = url.endsWith('checksums.txt');
    response.statusCode = checksum ? (options.noChecksum ? 404 : 200) : (options.status || 200);
    response.headers = { 'content-length': String(bytes.length) };
    setImmediate(() => {
      callback(response);
      if (checksum) response.end(`${options.badHash ? '0'.repeat(64) : hash}  libDTTPlugin-linux.so\n`);
      else {
        response.write(bytes.subarray(0, 5));
        setImmediate(() => {
          if (options.abort) response.destroy(new Error('connection interrupted'));
          else response.end(bytes.subarray(5));
        });
      }
    });
    return request;
  } };
  const original = Module._load;
  Module._load = function(request, parent, ...rest) {
    if (parent?.filename.endsWith('/out/setup.js')) {
      if (request === 'vscode') return vscode;
      if (request === 'os') return { homedir: () => home, platform: () => options.platform || 'linux', arch: () => options.arch || 'x64' };
      if (request === 'https') return https;
      if (request === './runner' && options.probe) return { Runner: class {
        async run() { return options.probe; }
      } };
      if (request === 'child_process' && options.llvmAsset) return {
        execFileSync(command, args) {
          assert.equal(command, 'tar');
          assert.ok(args.includes('--strip-components=1'));
          const bin = path.join(args[args.indexOf('-C') + 1], 'bin');
          fs.mkdirSync(bin, { recursive: true });
          fs.writeFileSync(path.join(bin, options.platform === 'win32' ? 'clang++.exe' : 'clang++'), 'compiler');
        }
      };
      if (request === 'child_process' && options.compilers) return {
        execSync(command) { return command.endsWith('clang++-18') ? options.compilers.versioned : options.compilers.generic; },
        execFileSync(file) { return options.compilers.versions[file] || 'clang version 17.0.0'; }
      };
      if (request === 'fs' && options.writeError) return { ...fs, createWriteStream: () => new Writable({
        write(chunk, encoding, callback) { callback(new Error('disk write failed')); }
      }) };
    }
    return original.call(this, request, parent, ...rest);
  };
  const location = require.resolve('../../ide-plugins/vscode-dtt/out/setup.js');
  delete require.cache[location];
  let setup;
  try { setup = require(location); } finally { Module._load = original; }
  return { setup, plugin, directory, home, errors, updates, messages };
}

for (const options of [{ status: 404 }, { abort: true }, { badHash: true }, { noChecksum: true }, { writeError: true }]) {
  test(`failed download leaves no installed plugin: ${JSON.stringify(options)}`, async t => {
    const f = fixture(t, options);
    await assert.rejects(f.setup.downloadPlugin({}));
    assert.equal(f.setup.findPlugin({}), null);
    assert.deepEqual(fs.readdirSync(f.directory), []);
  });
}

test('verified download replaces an existing plugin only after completion', async t => {
  const f = fixture(t, { existing: 'old plugin' });
  assert.equal(await f.setup.downloadPlugin({}), f.plugin);
  assert.deepEqual(fs.readFileSync(f.plugin), bytes);
  assert.deepEqual(fs.readdirSync(f.directory), ['libDTTPlugin.so']);
});

test('checksum failure preserves an existing plugin', async t => {
  const f = fixture(t, { existing: 'old plugin', badHash: true });
  await assert.rejects(f.setup.downloadPlugin({}));
  assert.equal(fs.readFileSync(f.plugin, 'utf8'), 'old plugin');
});

test('compiler discovery prefers compatible Clang over an incompatible PATH compiler', t => {
  const compilers = { generic: process.execPath, versioned: '/usr/bin/clang++-18',
    versions: { [process.execPath]: 'clang version 21.0.0', '/usr/bin/clang++-18': 'Ubuntu clang version 18.1.3' } };
  const f = fixture(t, { compilers });
  assert.equal(f.setup.findClang(), '/usr/bin/clang++-18');
});

test('compiler discovery rejects an unsupported configured compiler', t => {
  const f = fixture(t, { settings: { clangPath: process.execPath }, compilers: {
    generic: process.execPath, versioned: '', versions: { [process.execPath]: 'clang version 21.0.0' }
  } });
  assert.equal(f.setup.findClang(), null);
});

test('compiler discovery accepts a compatible configured compiler', t => {
  const f = fixture(t, { settings: { clangPath: process.execPath }, compilers: {
    generic: '/usr/bin/clang++-18', versioned: '', versions: { [process.execPath]: 'clang version 18.1.8' }
  } });
  assert.equal(f.setup.findClang(), process.execPath);
});

for (const probe of [
  { status: 'completed', exitCode: 1, output: 'error: unable to load plugin: libz3.so.4: cannot open shared object file' },
  { status: 'failed', exitCode: -1, output: 'compiler crashed' },
  { status: 'completed', exitCode: 0, output: '' },
  { status: 'completed', exitCode: 1, output: 'check.cpp:1:1: error: DTT: dereference of null pointer' }
]) {
  test(`setup validates that the plugin actually runs: ${probe.output || 'no diagnostics'}`, async t => {
    const f = fixture(t, { existing: 'plugin', settings: { clangPath: process.execPath },
      compilers: { versions: { [process.execPath]: 'clang version 18.1.8' } }, probe });
    const result = await f.setup.ensureBinaries({});
    if (probe.output.includes('error: DTT:')) {
      assert.equal(result.pluginPath, f.plugin);
      assert.ok(f.messages.includes('DTT is ready!'));
    } else {
      assert.equal(result, null);
      assert.equal(f.updates.length, 0);
      assert.ok(f.errors.some(message => /could not run/i.test(message)));
      assert.ok(!f.messages.includes('DTT is ready!'));
    }
  });
}

// Names verified against llvm-project's llvmorg-18.1.8 release assets.
for (const [platform, arch, suffix] of [
  ['linux', 'x64', 'x86_64-linux-gnu-ubuntu-18.04'],
  ['linux', 'arm64', 'aarch64-linux-gnu'],
  ['darwin', 'arm64', 'arm64-apple-macos11'],
  ['win32', 'x64', 'x86_64-pc-windows-msvc']
]) {
  test(`LLVM download uses a published archive and extraction layout: ${platform}/${arch}`, async t => {
    const f = fixture(t, { platform, arch, llvmAsset: `clang+llvm-18.1.8-${suffix}.tar.xz` });
    const compiler = await f.setup.downloadLLVM({});
    assert.equal(fs.readFileSync(compiler, 'utf8'), 'compiler');
  });
}
for (const [platform, arch] of [['darwin', 'x64'], ['win32', 'arm64'], ['linux', 'ia32']]) {
  test(`LLVM platforms without a supported archive get installation guidance: ${platform}/${arch}`, async t => {
    const f = fixture(t, { platform, arch, noLLVMAsset: true });
    await assert.rejects(f.setup.downloadLLVM({}), /install|build/i);
  });
}
