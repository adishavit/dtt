import { readFile } from 'node:fs/promises';
import { Sandbox } from 'railway';
import { createDemoServer } from './http.mjs';

if (!process.env.RAILWAY_TOKEN || !process.env.DTT_CHECKPOINT)
  throw Error('RAILWAY_TOKEN and DTT_CHECKPOINT are required.');

async function compile(input) {
  let sandbox;
  try {
    sandbox = await Sandbox.create(process.env.DTT_CHECKPOINT, {
      idleTimeoutMinutes: 1, networkIsolation: 'ISOLATED',
      fetch: (url, options) => fetch(url, {...options, signal: AbortSignal.timeout(20000)}),
    });
    await sandbox.files.write('/opt/dtt/request.json', JSON.stringify(input));
    const result = await sandbox.exec('timeout -k 2 15 python3 /opt/dtt/compile.py', {
      timeoutSec: 20, maxOutputBytes: 300000, ephemeral: true,
    });
    if (result.exitCode !== 0 || result.timedOut || result.truncated) throw Error('Runner failed');
    const response = JSON.parse(result.stdout);
    if (typeof response.output !== 'string' || !Number.isInteger(response.returncode))
      throw Error('Invalid compiler response');
    return response;
  } finally {
    if (sandbox) await sandbox.destroy();
  }
}

const html = await readFile(new URL('./index.html', import.meta.url), 'utf8');
const server = createDemoServer({compile, html});
server.listen(Number(process.env.PORT || 8080), '0.0.0.0');
process.on('SIGTERM', () => server.close());
