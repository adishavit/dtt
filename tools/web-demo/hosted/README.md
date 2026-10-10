# Hosted DTT editor

Serves the existing web editor using Node 22 and the pinned Railway Sandbox SDK.
The local Flask server remains available for local use. No slides are included.

The first uncached compilation boots a checkpoint in an isolated Railway VM.
Successive edits reuse that VM until it has been idle for 30 seconds, then it is
destroyed. Failures discard it; an expired VM is replaced on the next request.
A one-minute provider idle timeout covers abandoned VMs. Request files are
removed after every compilation. Each invocation of `compile.py` starts a fresh
compiler process inside Bubblewrap with filesystem, PID, IPC, UTS, and network
namespaces. `setpriv`
drops to UID/GID 65534, clears supplementary groups and the capability bounding
set, and forbids acquiring new privileges. The compiler sees read-only system
tools/headers, the plugin, and its source file, plus private temporary storage.
It receives no Railway credentials. Source is checked with `-fsyntax-only`;
submitted programs are never executed.

Limits: 32 KiB source, 512 MiB address space, 5 CPU seconds, 10 wall seconds,
128 KiB compiler output, one in-flight job, and 20 new jobs per minute across
the service. A bounded in-memory result cache avoids recompiling identical
requests. These limits assume one server process and one replica. Rejected
requests get an explicit busy/invalid/unavailable response. Infrastructure
failures are not shown as successful compilation.

## Prepare the compiler checkpoint

Use the matching Ubuntu 24.04 Clang 18 plugin binary. Install dependencies with
`npm ci --prefix tools/web-demo/hosted --ignore-scripts`. Authenticate the SDK
with a Railway project token in `RAILWAY_TOKEN` and set `RAILWAY_ENVIRONMENT_ID`.
Keep credentials out of source files and upload directories.

Create a temporary sandbox with the Railway CLI, allowing five idle minutes.
Then run:

```sh
node tools/web-demo/hosted/prepare.mjs SANDBOX_ID build/libDTTPlugin.so dtt-clang18-v1
```

This installs Clang/Z3/Bubblewrap, uploads the plugin and runner, verifies safe
and unsafe compilation plus blocked host-file access, saves a checkpoint only
after those checks pass, and destroys the preparation VM. Rebuild the checkpoint
with a new name when the plugin or runner changes.

## Deploy

Create a Railway service with these variables:

- `RAILWAY_TOKEN`: project token scoped to the production environment, stored
  only as a service variable. CLI OAuth cannot create permanent project tokens;
  create this in the project's Settings → Tokens page.
- `DTT_CHECKPOINT`: verified checkpoint name.
- Railway supplies `RAILWAY_ENVIRONMENT_ID` and `PORT` automatically.

Set the service healthcheck to `/health`, enable idle sleep, and use one replica.
Run `python3 tools/web-demo/hosted/stage.py`, then deploy only the printed staging
directory with `railway up STAGING_DIRECTORY --path-as-root`, specifying the
project, environment, and service. Generate an
HTTPS domain targeting port 8080 after verifying the deployment. Never upload
the repository root or any `talks/` content.

The first request can take longer while a compiler VM boots. Sandbox VM pricing
differs from normal Railway services; monitor the workspace's usage. This setup
does not upgrade the account or add a payment method. Service rate limits are
resource controls, not a durable billing cap.

## Verification

```sh
node --test examples/review/web_hosted.test.mjs examples/review/web_warm.test.mjs
```

`prepare.mjs` performs real compiler/isolation checks before checkpoint creation.
With SDK credentials and `DTT_CHECKPOINT` set,
`node examples/review/web_warm_live.mjs` verifies actual VM reuse, request-file
cleanup, isolation, and shutdown.
After deployment, verify `/health`, safe and unsafe `/compile` requests, preset
diagnostics, request rejection, and the browser UI. The C++ plugin suite remains
the normal CTest suite and should run in the background.
