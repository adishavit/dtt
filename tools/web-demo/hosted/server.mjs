import { readFile } from 'node:fs/promises';
import { Sandbox } from 'railway';
import { createDemoServer } from './http.mjs';
import { createWarmCompiler } from './warm.mjs';

if (!process.env.RAILWAY_TOKEN || !process.env.DTT_CHECKPOINT)
  throw Error('RAILWAY_TOKEN and DTT_CHECKPOINT are required.');

const compiler = createWarmCompiler({
  createSandbox: () => Sandbox.create(process.env.DTT_CHECKPOINT, {
    idleTimeoutMinutes: 1, networkIsolation: 'ISOLATED',
    fetch: (url, options) => fetch(url, {...options, signal: AbortSignal.timeout(20000)}),
  }),
});

const html = await readFile(new URL('./index.html', import.meta.url), 'utf8');
const server = createDemoServer({compile: compiler.compile, prewarm: compiler.prewarm, html});
server.listen(Number(process.env.PORT || 8080), '0.0.0.0');
process.on('SIGTERM', () => { server.close(); void compiler.close(); });
