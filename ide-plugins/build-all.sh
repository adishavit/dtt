#!/bin/bash
# Build all IDE plugins
# Usage: ./build-all.sh [--skip-vs]

set -e
cd "$(dirname "$0")"

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
cd vscode-dtt
npm install --silent
npm run compile
npm test
echo "✓ VS Code: OK"
cd ..

# CLion (Kotlin/Gradle) - requires Java 17+
echo ""
echo ">>> CLion Plugin"
cd clion-dtt
if [ -f "./gradlew" ] && [ -d "./gradle/wrapper" ]; then
    ./gradlew build --quiet && echo "✓ CLion: OK" || echo "⚠ CLion: Build failed"
elif command -v gradle &> /dev/null; then
    # Bootstrap wrapper if missing
    echo "Bootstrapping Gradle wrapper..."
    gradle wrapper --gradle-version 8.5 2>/dev/null || true
    if [ -f "./gradlew" ]; then
        ./gradlew build --quiet && echo "✓ CLion: OK" || echo "⚠ CLion: Build failed"
    else
        echo "⚠ CLion: Skipped (could not create Gradle wrapper)"
    fi
else
    echo "⚠ CLion: Skipped (gradle not installed, no wrapper present)"
fi
cd ..

# Visual Studio (C#/.NET)
echo ""
echo ">>> Visual Studio Extension"
cd vs-dtt
if [ "$SKIP_VS" = true ]; then
    echo "⚠ VS: Skipped (--skip-vs)"
elif command -v dotnet &> /dev/null; then
    dotnet build --nologo -v q 2>&1 | tail -5 || echo "⚠ VS: Build failed (needs Windows + VS SDK)"
else
    echo "⚠ VS: Skipped (dotnet not installed)"
fi
cd ..

# Interpreted plugins (no build needed)
echo ""
echo ">>> Neovim (Lua) - no build needed ✓"
echo ">>> Vim (VimScript) - no build needed ✓"
echo ">>> Emacs (Elisp) - no build needed ✓"

echo ""
echo "=== Build Complete ==="
