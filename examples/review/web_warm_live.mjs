// Real Railway integration check. Credentials are supplied by the operator.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {createWarmCompiler} from '../../tools/web-demo/hosted/warm.mjs';
const require = createRequire(new URL('../../tools/web-demo/hosted/package.json', import.meta.url));
const {Sandbox} = require('railway');
let created = 0, sandbox;
const runner = createWarmCompiler({createSandbox: async () => {
  created++;
  sandbox = await Sandbox.create(process.env.DTT_CHECKPOINT, {
    idleTimeoutMinutes: 1, networkIsolation: 'ISOLATED',
  });
  return sandbox;
}});
try {
  const start = performance.now();
  await Promise.all([runner.prewarm(), runner.prewarm()]);
  assert.equal(created, 1, 'simultaneous page loads must share startup');
  console.log(`Prewarming: passed, ${((performance.now() - start) / 1000).toFixed(2)}s`);
  for (const [name, code, expected] of [
    ['prewarmed safe', '#include <vector>\nvoid f(){std::vector<int> v{1}; v.front();}', 0],
    ['warm unsafe', '#include <vector>\nvoid f(){std::vector<int> v; v.front();}', 1],
    ['warm isolation', '#include "/opt/dtt/compile.py"', 1],
  ]) {
    const start = performance.now();
    const result = await runner.compile({code, mode: 'enforce'});
    assert.equal(result.returncode, expected, result.output);
    if (name.includes('unsafe')) assert.match(result.output, /DTT: vector::front/);
    if (name.includes('isolation')) assert.match(result.output, /file not found/);
    assert.equal(await sandbox.files.exists('/opt/dtt/request.json'), false);
    console.log(`${name}: passed, ${((performance.now() - start) / 1000).toFixed(2)}s`);
  }
  assert.equal(created, 1, 'all requests must reuse the same VM');
} finally {
  await runner.close();
}
console.log('Real warm VM reuse, source cleanup, isolation and shutdown: passed');
