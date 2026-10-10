import { createServer } from 'node:http';
import { createHash } from 'node:crypto';

const error = output => ({success: false, returncode: -1, output, diagnostics: []});

export function createDemoServer({compile, html, prewarm = async () => {}, jobsPerMinute = 20}) {
  let busy = false;
  const jobs = [], cache = new Map();
  const server = createServer(async (req, res) => {
    const send = (status, body) => {
      res.writeHead(status, {'content-type': 'application/json', 'cache-control': 'no-store',
        'x-content-type-options': 'nosniff', ...(status === 429 ? {'retry-after': '5'} : {})});
      res.end(JSON.stringify(body));
    };
    if (req.method === 'GET' && req.url === '/health') return send(200, {status: 'ok'});
    if (req.method === 'GET' && (req.url === '/' || req.url.startsWith('/?'))) {
      void Promise.resolve().then(prewarm).catch(() => {
        console.error('Compiler prewarming failed.');
      });
      res.writeHead(200, {'content-type': 'text/html; charset=utf-8',
        'x-content-type-options': 'nosniff', 'referrer-policy': 'no-referrer'});
      return res.end(html);
    }
    if (req.method !== 'POST' || req.url !== '/compile') return send(404, error('Not found.'));
    if (!req.headers['content-type']?.startsWith('application/json'))
      return send(415, error('Send JSON source code.'));
    let data;
    try {
      let size = 0;
      const chunks = [];
      for await (const chunk of req) {
        size += chunk.length;
        if (size > 65536) { send(413, error('Source is too large (32 KiB maximum).')); return; }
        chunks.push(chunk);
      }
      data = JSON.parse(Buffer.concat(chunks).toString('utf8'));
    } catch { return send(400, error('Invalid JSON.')); }
    if (!data || typeof data.code !== 'string' ||
        (data.mode !== undefined && !['enforce', 'inform', 'quiet'].includes(data.mode)))
      return send(400, error('Provide code and a valid DTT mode.'));
    if (Buffer.byteLength(data.code) > 32768) return send(413, error('Source is too large (32 KiB maximum).'));
    const input = {code: data.code, mode: data.mode ?? 'enforce'};
    const key = createHash('sha256').update(JSON.stringify(input)).digest('hex');
    if (cache.has(key)) return send(200, cache.get(key));
    const now = Date.now();
    while (jobs.length && jobs[0] < now - 60000) jobs.shift();
    if (busy || jobs.length >= jobsPerMinute)
      return send(429, error('Compiler busy. Please try again in a few seconds.'));
    busy = true;
    jobs.push(now);
    try {
      const result = await compile(input);
      const diagnostics = [...result.output.matchAll(/^input\.cpp:(\d+):(\d+):\s+(error|warning|note):\s+(.*)$/gm)]
        .map(m => ({line: +m[1], column: +m[2], severity: m[3], message: m[4]}));
      const body = {...result, success: result.returncode === 0, diagnostics,
        output: result.output || '(No output - compilation successful)'};
      if ([0, 1].includes(result.returncode)) {
        if (cache.size >= 64) cache.delete(cache.keys().next().value);
        cache.set(key, body);
      }
      send(200, body);
    } catch {
      console.error('Compiler infrastructure request failed.');
      send(503, error('Compiler unavailable. Please retry shortly.'));
    } finally { busy = false; }
  });
  server.requestTimeout = 10000;
  server.headersTimeout = 10000;
  server.maxHeadersCount = 30;
  return server;
}
