// End-to-end check against a deployed service; source is never executed.
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import vm from 'node:vm';
import { spawnSync } from 'node:child_process';

const [base, plugin] = process.argv.slice(2);
if (!base || !plugin) throw Error('Usage: web_live_smoke.mjs URL PLUGIN');
const html = await readFile('tools/web-demo/index.html', 'utf8');
const match = html.match(/const examples = (\{[\s\S]*?^        \});/m);
assert.ok(match, 'preset definitions found');
const examples = vm.runInNewContext('(' + match[1] + ')');
assert.equal((await fetch(base + '/health')).status, 200);
assert.match(await (await fetch(base)).text(), /Monaco|monaco/);
async function post(body) {
  return fetch(base + '/compile', {method: 'POST', headers: {'content-type': 'application/json'},
    body: JSON.stringify(body), signal: AbortSignal.timeout(120000)});
}
for (const [name, code] of Object.entries(examples)) {
  const response = await post({code, mode: 'enforce'});
  assert.equal(response.status, 200, await response.clone().text());
  const remote = await response.json();
  const local = spawnSync('clang++-18', ['-fplugin=' + plugin, '-std=c++20', '-fsyntax-only',
    '-Wno-unused-result', '-Wno-unused-value', '-fplugin-arg-dtt--dtt-mode=enforce', '-x', 'c++', '-'],
    {input: code, encoding: 'utf8', timeout: 15000});
  assert.equal(remote.returncode, local.status, name + ': exit status differs');
  const locations = [...local.stderr.matchAll(/^<stdin>:(\d+):(\d+): (error|warning):/gm)]
    .map(m => [+m[1], +m[2], m[3]]);
  assert.deepEqual(remote.diagnostics.filter(d => d.severity !== 'note')
    .map(d => [d.line, d.column, d.severity]), locations, name + ': diagnostic locations differ');
  console.log(name + ': matches local compiler');
}
let response = await post({code: '#include <vector>\nvoid f(){std::vector<int> v{1}; v.front();}', mode: 'enforce'});
assert.equal(response.status, 200);
assert.equal((await response.json()).success, true);
response = await post({code: '#include "/opt/dtt/compile.py"', mode: 'enforce'});
assert.equal(response.status, 200);
assert.match((await response.json()).output, /file not found/);
assert.equal((await post({code: 'x'.repeat(32769)})).status, 413);
assert.equal((await post({code: 'int x;', mode: 'invalid'})).status, 400);
console.log('Safe code, host-file isolation, and request limits: passed');
