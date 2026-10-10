# Railway runtime probe

Finite, private compatibility test for the live compiler's hosting requirements.
This is not the public demo server or a production sandbox.

Stage only `Dockerfile`, `railway.json`, `probe.py`, and a locally built
`libDTTPlugin.so` in a separate upload directory. The plugin must match Ubuntu
24.04's Clang 18. Do not upload the repository root: it contains private talks.

The image installs Clang, Z3, and Bubblewrap. It checks compilation of safe and
unsafe vector access, then attempts filesystem/process/network namespaces and
chroot. Each subprocess has CPU, memory, and wall-clock limits. The whole probe
has a one-minute deadline, opens no listening port, and requests no restarts.

Deploy that staging directory to a temporary Railway service. Read its runtime
logs, verify `probe_complete`, then remove the deployment/project. Do not infer
isolation support from a successful image build; inspect the runtime results.

A successful chroot capability check alone is not sufficient isolation for a
public compiler. Resource limits and restricted filesystem, environment,
privileges, network access, and process cleanup still need integration and tests.

## Observed results — 2026-10-10

Plugin source checkout: `7e89780`; binary SHA-256:
`6eb04ed4b42b860d86edcf1d61a9770f3d0b46d32c7d2dfc528d87f068de5148`.
Railway runtime compiler: Ubuntu Clang 18.1.3, matching the local compiler.

| Standard service probe | Result |
|---|---|
| Safe vector access after push_back | Exit 0, no diagnostic |
| Empty vector front | Exit 1, DTT proven-unsafe diagnostic |
| Bubblewrap with --unshare-all | Permission denied |
| User / network namespace creation | Permission denied / operation not permitted |
| chroot capability | Succeeded |

A separate Railway native Sandbox allowed network namespaces and successfully
ran `/bin/true` under Bubblewrap with `--unshare-pid --unshare-net --unshare-ipc
--unshare-uts`, a read-only `/usr`, synthetic `/proc` and `/dev`, and temporary
`/tmp`. User namespace creation was rejected there too; this successful probe
used the Sandbox's existing privileges instead. This establishes capability,
not validation of a complete public compilation service. DTT was tested in the
standard service; DTT inside this restricted Sandbox filesystem remains untested.

The account was trial-only with $5 credit, no payment method, and no paid usage
subscription. Railway rejected a workspace hard limit of $5 (minimum $10).
No upgrade or larger limit was applied. Usage reporting still showed $0 at the
end of the probes; billing telemetry can lag. No public domain was created.

Hosting the editor requires a production HTTP server and a bounded compiler
runner integrated with isolation. Railway native Sandboxes are a possible
backend, but introduce API/template/lifecycle integration and have different
pricing from normal services. They are not a drop-in Flask deployment.
