// One warm VM; each request still invokes a fresh Bubblewrap-isolated compiler.
export function createWarmCompiler({createSandbox, idleMs = 30000,
  schedule = setTimeout, cancel = clearTimeout}) {
  let sandbox = null, timer = null, busy = false, closed = false, generation = 0;
  let starting = null;

  function ensureSandbox() {
    if (sandbox) return Promise.resolve(sandbox);
    if (!starting) {
      starting = Promise.resolve().then(createSandbox).then(vm => {
        sandbox = vm;
        return vm;
      }).finally(() => { starting = null; });
    }
    return starting;
  }

  async function discard() {
    const previous = sandbox;
    sandbox = null;
    if (previous) {
      try { await previous.destroy(); }
      catch { console.error('Sandbox cleanup failed; provider idle timeout remains enabled.'); }
    }
  }

  function cancelIdle() {
    generation++;
    if (timer !== null) cancel(timer);
    timer = null;
  }

  function armIdle() {
    if (!sandbox || busy || timer !== null) return;
    const currentGeneration = generation;
    timer = schedule(async () => {
      if (busy || currentGeneration !== generation) return;
      timer = null;
      await discard();
    }, idleMs);
    timer?.unref?.();
  }

  return {
    async prewarm() {
      if (closed || busy || sandbox) return;
      await ensureSandbox();
      if (closed) {
        if (!busy) await discard();
      } else armIdle();
    },
    async compile(input) {
      if (closed || busy) throw Error('Compiler is unavailable');
      busy = true;
      cancelIdle();
      try {
        const reused = sandbox !== null;
        await ensureSandbox();
        try {
          await sandbox.files.write('/opt/dtt/request.json', JSON.stringify(input));
        } catch (error) {
          if (!reused) throw error;
          // An idle VM may have been reaped by Railway. Nothing has run yet.
          await discard();
          await ensureSandbox();
          await sandbox.files.write('/opt/dtt/request.json', JSON.stringify(input));
        }
        const result = await sandbox.exec('timeout -k 2 15 python3 /opt/dtt/compile.py', {
          timeoutSec: 20, maxOutputBytes: 300000, ephemeral: true,
        });
        if (result.exitCode !== 0 || result.timedOut || result.truncated) throw Error('Runner failed');
        const response = JSON.parse(result.stdout);
        if (typeof response.output !== 'string' || !Number.isInteger(response.returncode))
          throw Error('Invalid compiler response');
        await sandbox.files.remove('/opt/dtt/request.json');
        if (![0, 1].includes(response.returncode)) await discard();
        return response;
      } catch (error) {
        await discard();
        throw error;
      } finally {
        busy = false;
        if (closed) await discard();
        else armIdle();
      }
    },
    async close() {
      closed = true;
      cancelIdle();
      if (!busy) {
        try { await starting; } catch { /* No VM to clean up after a failed startup. */ }
        await discard();
      }
    },
  };
}
