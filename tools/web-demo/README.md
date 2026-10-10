# DTT web demo

A small live-compile editor for trying DTT in the browser: edit C++ on the
left, see the plugin's diagnostics on the right. Twelve presets cover vectors,
`std::optional`, use-after-free, ghost state, state machines and Z3 reasoning.
Each preset marks its expected diagnostics with `// ERROR` and `// OK`.

[Try the hosted editor](https://web-demo-production-32ce.up.railway.app/).

## Run

```bash
# build the plugin once (from the repository root)
cmake -B build -DCMAKE_CXX_COMPILER=clang++-18 && cmake --build build

pip install -r tools/web-demo/requirements.txt
python tools/web-demo/app.py            # http://127.0.0.1:5000
```

| Variable | Meaning |
|----------|---------|
| `DTT_PLUGIN` | Path to `libDTTPlugin.so` (default: `build/libDTTPlugin.so`) |
| `PORT` | Port to listen on (default: `5000`) |

Requires `clang++-18` on `PATH`. Monaco, anime.js and the fonts load from a CDN,
so the browser needs internet access.

The Flask server compiles whatever source is posted to `/compile`, so it listens
on localhost only. Do not expose that server to a network. The separate
[hosted backend](hosted/README.md) runs compilation in disposable Railway VMs.

## Controls

- `Ctrl+Enter` compiles; edits also recompile after a short pause.
- `?example=NAME` opens a preset directly (for example `?example=vector-bounds`).
