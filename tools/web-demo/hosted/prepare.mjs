// Operator-only: populate and verify a disposable VM before checkpointing it.
// Reads Railway credentials from the environment, never embeds them in the VM.
import { readFile } from 'node:fs/promises';
import { Sandbox } from 'railway';
import assert from 'node:assert/strict';

const [id, pluginPath, checkpoint] = process.argv.slice(2);
if (!id || !pluginPath || !checkpoint) throw Error('Usage: prepare.mjs SANDBOX PLUGIN CHECKPOINT');
const sandbox = await Sandbox.connect(id);
try {
  const install = await sandbox.exec('timeout -k 5 180 sh -c "apt-get update -qq && apt-get install -y -qq --no-install-recommends clang-18 libz3-4 bubblewrap python3 util-linux"',
    {timeoutSec: 190, maxOutputBytes: 4000});
  assert.equal(install.exitCode, 0, install.stderr);
  console.log('Compiler packages installed.');
  await sandbox.files.write('/opt/dtt/libDTTPlugin.so', await readFile(pluginPath));
  await sandbox.files.write('/opt/dtt/compile.py', await readFile(new URL('./compile.py', import.meta.url)));
  await sandbox.files.write('/opt/dtt/private.h', '#error HOST_FILE_EXPOSED');
  const cases = [
    ['safe', '#include <vector>\nvoid f(){std::vector<int> v; v.push_back(1); v.front();}', 0],
    ['unsafe', '#include <vector>\nvoid f(){std::vector<int> v; v.front();}', 1],
    ['filesystem isolation', '#include "/opt/dtt/private.h"', 1],
    ['host proc isolation', '#include "/proc/1/root/opt/dtt/private.h"', 1],
  ];
  for (const [name, code, expected] of cases) {
    await sandbox.files.write('/opt/dtt/request.json', JSON.stringify({code, mode: 'enforce'}));
    const result = await sandbox.exec('python3 /opt/dtt/compile.py', {timeoutSec: 20, maxOutputBytes: 300000, ephemeral: true});
    assert.equal(result.exitCode, 0, result.stderr);
    const compiled = JSON.parse(result.stdout);
    assert.equal(compiled.returncode, expected, JSON.stringify(compiled));
    if (name === 'unsafe') assert.match(compiled.output, /DTT: vector::front/);
    if (name.includes('isolation')) {
      assert.match(compiled.output, /file not found|Permission denied/);
      assert.doesNotMatch(compiled.output, /error: HOST_FILE_EXPOSED/);
    }
    console.log(`${name}: passed`);
  }
  await sandbox.files.remove('/opt/dtt/request.json');
  await sandbox.files.remove('/opt/dtt/private.h');
  const saved = await sandbox.checkpoint(checkpoint);
  console.log('Checkpoint ready:', saved.key);
} finally {
  await sandbox.destroy();
}
