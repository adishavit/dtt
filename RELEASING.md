# DTT Release Process

## Semantic Versioning

DTT follows [Semantic Versioning 2.0.0](https://semver.org/):

```
MAJOR.MINOR.PATCH[-PRERELEASE]
```

### Version Components

| Component | Increment When | Examples |
|-----------|---------------|----------|
| **MAJOR** | Breaking changes to user-facing behavior | API changes, removed features, changed defaults that break existing code |
| **MINOR** | New features, new checks, IDE plugin features | New null-safety check, new mode, new VS Code command |
| **PATCH** | Bug fixes, documentation, internal improvements | False positive fix, crash fix, test improvements |

### Pre-release Tags

| Tag | Purpose | Example |
|-----|---------|---------|
| `-alpha.N` | Early development, unstable | `v0.3.0-alpha.1` |
| `-beta.N` | Feature complete, testing | `v0.3.0-beta.1` |
| `-rc.N` | Release candidate | `v0.3.0-rc.1` |

## When to Release

### PATCH Release (v0.2.x)

- Fixed a false positive (DTT was incorrectly flagging safe code)
- Fixed a crash or hang
- Fixed IDE plugin bugs
- Documentation updates
- CI/build improvements

### MINOR Release (v0.x.0)

- New safety check category (e.g., new null-deref pattern detected)
- New `--dtt-mode` option
- New IDE plugin feature (e.g., new command, new configuration option)
- Significant improvement to detection (fewer false negatives)
- New platform support

### MAJOR Release (vX.0.0)

- Changed CLI argument format
- Changed diagnostic output format (breaks tooling)
- Removed a feature or mode
- Changed default behavior in breaking way
- Dropped platform support

## Release Checklist

### 1. Prepare

```bash
# Ensure main is up to date
git checkout main
git pull origin main

# Run full test suite
cmake -B build -DCMAKE_CXX_COMPILER=clang++-18
cmake --build build
ctest --test-dir build --output-on-failure
```

### 2. Update Version

Update version in:
- `ide-plugins/vscode-dtt/package.json` - `"version": "X.Y.Z"`
- `ide-plugins/vs-dtt/source.extension.vsixmanifest` - `Version="X.Y.Z"`

### 3. Write Release Notes

Create a draft in `docs/releases/vX.Y.Z.md`:

```markdown
# DTT vX.Y.Z

Released: YYYY-MM-DD

## Highlights

Brief (2-3 sentences) summary of what's notable in this release.

## New Features

- **Feature name**: Description (#PR)

## Bug Fixes

- Fixed false positive when ... (#PR)
- Fixed crash on ... (#PR)

## Improvements

- Improved detection of ... (#PR)
- Documentation: ... (#PR)

## Breaking Changes

(Only for MAJOR releases)

- Changed X to Y. Migration: ...

## Known Issues

- Issue description (#issue)

## Contributors

Thanks to @contributor1, @contributor2 for their contributions!
```

### 4. Create Release

```bash
# Create and push tag
git tag -a v0.2.0 -m "Release v0.2.0"
git push origin v0.2.0
```

The GitHub Actions workflow will automatically:
1. Build DTT plugin for Linux and macOS
2. Build VS Code extension
3. Create GitHub Release with all artifacts
4. Generate checksums

### 5. Post-Release

- Announce on relevant channels
- Update documentation if needed
- Close milestone (if used)

## Artifact Naming Convention

| Platform | Plugin Filename | IDE Extension |
|----------|----------------|---------------|
| Linux | `libDTTPlugin-linux.so` | `dtt-X.Y.Z.vsix` |
| macOS | `libDTTPlugin-darwin.dylib` | `dtt-X.Y.Z.vsix` |
| Windows | `DTTPlugin-win64.dll` | `DTT.vsix` |

## Version Syncing

Keep versions synchronized across:

```
DTT Plugin (core)     ─┬─► vscode-dtt/package.json
                       └─► vs-dtt/source.extension.vsixmanifest
```

IDE extensions should match the core DTT version they're built against.

## Emergency Hotfix Process

For critical bugs in a released version:

```bash
# Branch from release tag
git checkout -b hotfix/v0.2.1 v0.2.0

# Fix the issue, commit
git commit -m "fix: critical bug description"

# Tag and release
git tag -a v0.2.1 -m "Hotfix: critical bug"
git push origin v0.2.1 hotfix/v0.2.1

# Merge back to main
git checkout main
git merge hotfix/v0.2.1
git push origin main
```

## CI/CD Pipeline

```
                    ┌─────────────────┐
                    │  Push tag v*    │
                    └────────┬────────┘
                             │
              ┌──────────────┼──────────────┐
              ▼              ▼              ▼
        ┌──────────┐  ┌──────────┐  ┌──────────┐
        │  Linux   │  │  macOS   │  │  VS Code │
        │  Build   │  │  Build   │  │  Build   │
        └────┬─────┘  └────┬─────┘  └────┬─────┘
             │             │             │
             └──────────┬──┴─────────────┘
                        ▼
              ┌─────────────────┐
              │ Create Release  │
              │ Upload Artifacts│
              │ Generate Notes  │
              └─────────────────┘
```

### Manually Triggering a Release

If the automatic release failed or you need to re-release:

1. Go to Actions → Release workflow
2. Click "Run workflow"
3. Enter the tag (e.g., `v0.2.0`)
4. Click "Run workflow"
