import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createDemoServer } from '../../tools/web-demo/hosted/http.mjs';

async function fixture(t, compile, options = {}) {
  const server = createDemoServer({ compile, html: '<title>DTT</title>', ...options });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  t.after(() => new Promise(resolve => server.close(resolve)));
  const base = `http://127.0.0.1:${server.address().port}`;
  return (body, path = '/compile') => fetch(base + path, {
    method: 'POST', headers: {'content-type': 'application/json'},
    body: JSON.stringify(body),
  });
}

test('reject invalid requests before invoking a compiler', async t => {
  const post = await fixture(t, () => assert.fail('compiler invoked'));
  for (const body of [null, [], {}, {code: 3}, {code: 'x', mode: 'shell'},
    {code: 'x'.repeat(32769)}, {code: 'é'.repeat(20000)}]) {
    const res = await post(body);
    assert.ok([400, 413].includes(res.status));
    assert.equal((await res.json()).success, false);
  }
  assert.equal((await post({}, '/secret')).status, 404);
});

test('safe and unsafe compiler results retain diagnostic locations; repeated code is cached', async t => {
  let calls = 0;
  const post = await fixture(t, async ({code}) => {
    calls++;
    return {returncode: code === 'safe' ? 0 : 1,
      output: code === 'safe' ? '' : 'input.cpp:2:5: error: DTT: bounds\n'};
  });
  assert.equal((await (await post({code: 'safe'})).json()).success, true);
  const result = await (await post({code: 'unsafe'})).json();
  assert.equal(result.success, false);
  assert.equal(result.diagnostics[0].line, 2);
  await post({code: 'unsafe'});
  assert.equal(calls, 2);
});

test('busy compiler rejects concurrent work and recovers after failure', async t => {
  let release, started;
  const entered = new Promise(resolve => { started = resolve; });
  const post = await fixture(t, async () => {
    started();
    await new Promise(resolve => { release = resolve; });
    throw Error('private token must never be exposed');
  });
  const first = post({code: 'one'});
  await entered;
  assert.equal((await post({code: 'two'})).status, 429);
  release();
  const response = await first;
  assert.equal(response.status, 503);
  assert.ok(!(await response.text()).includes('private token'));
});

test('rate limit prevents additional compiler jobs', async t => {
  const post = await fixture(t, async () => ({returncode: 0, output: ''}), {jobsPerMinute: 1});
  assert.equal((await post({code: 'one'})).status, 200);
  assert.equal((await post({code: 'two'})).status, 429);
});
