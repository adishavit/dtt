import {test} from 'node:test';
import assert from 'node:assert/strict';
import {createWarmCompiler} from '../../tools/web-demo/hosted/warm.mjs';

function fixture() {
  const created = [], timers = new Set();
  const factory = async () => {
    const vm = {destroyed: 0, writes: [], removed: [], failWrite: false, failExec: false,
      files: {
        write: async (path, value) => { if (vm.failWrite) throw Error('expired'); vm.writes.push(JSON.parse(value)); },
        remove: async path => { vm.removed.push(path); },
      },
      exec: async () => {
        if (vm.failExec) throw Error('disconnected');
        return {exitCode: 0, stdout: JSON.stringify({returncode: 0, output: ''})};
      },
      destroy: async () => { vm.destroyed++; },
    };
    created.push(vm);
    return vm;
  };
  const runner = createWarmCompiler({createSandbox: factory,
    schedule: callback => { timers.add(callback); return callback; },
    cancel: callback => timers.delete(callback)});
  const expire = async () => { const callbacks = [...timers]; timers.clear(); for (const f of callbacks) await f(); };
  return {runner, created, timers, expire};
}

test('different edits reuse the VM, remove each source request, and expire after idle', async () => {
  const {runner, created, timers, expire} = fixture();
  await runner.compile({code: 'first', mode: 'enforce'});
  await runner.compile({code: 'second', mode: 'inform'});
  assert.equal(created.length, 1);
  assert.deepEqual(created[0].writes.map(x => x.code), ['first', 'second']);
  assert.equal(created[0].removed.length, 2);
  assert.equal(timers.size, 1);
  await expire();
  assert.equal(created[0].destroyed, 1);
  await runner.compile({code: 'third'});
  assert.equal(created.length, 2);
  await runner.close();
  assert.equal(created[1].destroyed, 1);
  assert.equal(timers.size, 0);
  await assert.rejects(runner.compile({code: 'after shutdown'}));
});

test('a VM reaped by the provider is replaced once before compilation', async () => {
  const {runner, created} = fixture();
  await runner.compile({code: 'first'});
  created[0].failWrite = true;
  await runner.compile({code: 'second'});
  assert.equal(created.length, 2);
  assert.equal(created[0].destroyed, 1);
  assert.equal(created[1].writes[0].code, 'second');
  await runner.close();
});

test('execution failure discards the VM and subsequent compilation recovers', async () => {
  const {runner, created, timers} = fixture();
  await runner.compile({code: 'first'});
  created[0].failExec = true;
  await assert.rejects(runner.compile({code: 'second'}));
  assert.equal(created[0].destroyed, 1);
  assert.equal(timers.size, 0);
  await runner.compile({code: 'third'});
  assert.equal(created.length, 2);
  await runner.close();
});

test('idle callback cannot destroy a VM during the next compilation', async () => {
  const {runner, created, timers} = fixture();
  await runner.compile({code: 'first'});
  const oldCallback = [...timers][0];
  let release;
  created[0].exec = () => new Promise(resolve => { release = resolve; });
  const running = runner.compile({code: 'second'});
  await new Promise(resolve => setImmediate(resolve));
  await oldCallback();
  assert.equal(created[0].destroyed, 0);
  await assert.rejects(runner.compile({code: 'concurrent'}));
  release({exitCode: 0, stdout: JSON.stringify({returncode: 0, output: ''})});
  await running;
  await runner.close();
});
