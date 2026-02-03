#!/bin/bash
# Build all IDE plugins
# Usage: ./build-all.sh [--skip-vs]

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

SKIP_VS=false
for arg in "$@"; do
    case $arg in
        --skip-vs) SKIP_VS=true ;;
    esac
done

echo "=== Building IDE Plugins ==="

# VS Code (TypeScript)
echo ""
echo ">>> VS Code Extension"
pushd vscode-dtt > /dev/null
npm install --silent || { echo "✗ VS Code: npm install failed"; popd > /dev/null; exit 1; }
npm run compile || { echo "✗ VS Code: compilation failed"; popd > /dev/null; exit 1; }
npm test || { echo "✗ VS Code: tests failed"; popd > /dev/null; exit 1; }
npx vsce package --allow-missing-repository -o dtt.vsix 2>/dev/null && echo "✓ VS Code: OK (dtt.vsix created)" || echo "✓ VS Code: OK (vsce not available for packaging)"
popd > /dev/null

# CLion (Kotlin/Gradle) - requires Java 21+
echo ""
echo ">>> CLion Plugin"
pushd clion-dtt > /dev/null
if [ -f "./gradlew" ] && [ -d "./gradle/wrapper" ]; then
    ./gradlew build --quiet && echo "✓ CLion: OK" || { echo "✗ CLion: Build failed"; popd > /dev/null; exit 1; }
elif command -v gradle &> /dev/null; then
    # Bootstrap wrapper if missing
    echo "Bootstrapping Gradle wrapper..."
    if gradle wrapper --gradle-version 8.5 2>&1; then
        ./gradlew build --quiet && echo "✓ CLion: OK" || { echo "✗ CLion: Build failed"; popd > /dev/null; exit 1; }
    else
        echo "⚠ CLion: Skipped (could not create Gradle wrapper)"
    fi
else
    echo "⚠ CLion: Skipped (gradle not installed, no wrapper present)"
fi
popd > /dev/null

# Visual Studio (C#/.NET)
echo ""
echo ">>> Visual Studio Extension"
pushd vs-dtt > /dev/null
if [ "$SKIP_VS" = true ]; then
    echo "⚠ VS: Skipped (--skip-vs)"
elif command -v dotnet &> /dev/null; then
    if dotnet build --nologo -v q; then
        echo "✓ VS: OK"
    else
        echo "⚠ VS: Build failed (needs Windows + VS SDK)"
    fi
else
    echo "⚠ VS: Skipped (dotnet not installed)"
fi
popd > /dev/null

# Interpreted plugins (no build needed)
echo ""
echo ">>> Neovim (Lua) - no build needed ✓"
echo ">>> Vim (VimScript) - no build needed ✓"
echo ">>> Emacs (Elisp) - no build needed ✓"

echo ""
echo "=== Build Complete ==="
