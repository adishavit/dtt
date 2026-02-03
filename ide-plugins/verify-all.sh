#!/bin/bash
# verify-all.sh - Build and verify all DTT IDE plugins
# Usage: ./verify-all.sh [--install-deps] [--clean] [--package]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Results tracking
declare -A RESULTS

pass() { RESULTS["$1"]="${GREEN}✅ PASS${NC}"; }
fail() { RESULTS["$1"]="${RED}❌ FAIL${NC}"; }
skip() { RESULTS["$1"]="${YELLOW}⏭ SKIP${NC}"; }

log() { echo -e "${GREEN}>>>${NC} $1"; }
warn() { echo -e "${YELLOW}WARNING:${NC} $1"; }
err() { echo -e "${RED}ERROR:${NC} $1"; }

# Check if a command exists
has_cmd() { command -v "$1" &>/dev/null; }

#=============================================================================
# Clean all build artifacts
#=============================================================================
clean_all() {
    log "Cleaning all build artifacts..."

    # VS Code
    rm -rf vscode-dtt/out vscode-dtt/node_modules vscode-dtt/*.vsix 2>/dev/null || true

    # Visual Studio
    rm -rf vs-dtt/bin vs-dtt/obj vs-dtt/*.nupkg 2>/dev/null || true

    # CLion
    rm -rf clion-dtt/build clion-dtt/.gradle clion-dtt/.intellijPlatform 2>/dev/null || true

    # Emacs
    rm -f emacs-dtt/*.elc 2>/dev/null || true

    # Checksums
    rm -f checksums.txt 2>/dev/null || true

    # Dist folder
    rm -rf dist 2>/dev/null || true

    log "Clean complete"
}

# Install dependencies if requested
install_deps() {
    log "Installing dependencies..."

    if has_cmd apt-get; then
        sudo apt-get update -qq

        # Node.js
        if ! has_cmd node; then
            log "Installing Node.js..."
            curl -fsSL https://deb.nodesource.com/setup_lts.x | sudo -E bash -
            sudo apt-get install -y nodejs
        fi

        # .NET SDK
        if ! has_cmd dotnet; then
            log "Installing .NET SDK..."
            sudo apt-get install -y dotnet-sdk-8.0 || warn ".NET SDK install failed"
        fi

        # Java (for Gradle)
        if ! has_cmd java; then
            log "Installing Java..."
            sudo apt-get install -y default-jdk
        fi

        # Emacs
        if ! has_cmd emacs; then
            log "Installing Emacs..."
            sudo DEBIAN_FRONTEND=noninteractive apt-get install -y emacs-nox
        fi

        # Lua
        if ! has_cmd luac; then
            log "Installing Lua..."
            sudo apt-get install -y lua5.4
        fi

        # Vim
        if ! has_cmd vim; then
            log "Installing Vim..."
            sudo apt-get install -y vim
        fi
    else
        warn "apt-get not found. Please install dependencies manually:"
        echo "  - Node.js (npm)"
        echo "  - .NET SDK 8.0"
        echo "  - Java JDK 17+"
        echo "  - Emacs"
        echo "  - Lua 5.x"
        echo "  - Vim"
    fi
}

# Install Emacs flycheck package
install_flycheck() {
    local pkg_dir="/tmp/emacs-dtt-pkg"
    if [[ ! -d "$pkg_dir/flycheck"* ]]; then
        log "Installing Emacs flycheck package..."
        emacs --batch --eval "(progn
            (require 'package)
            (setq package-user-dir \"$pkg_dir\")
            (add-to-list 'package-archives '(\"melpa\" . \"https://melpa.org/packages/\") t)
            (package-initialize)
            (package-refresh-contents)
            (package-install 'flycheck))" 2>/dev/null || warn "flycheck install failed"
    fi
}

#=============================================================================
# VS Code Extension
#=============================================================================
build_vscode() {
    log "Building VS Code extension..."
    cd "$SCRIPT_DIR/vscode-dtt"

    if ! has_cmd npm; then
        skip "vscode"
        warn "npm not found"
        return
    fi

    # Install deps
    if ! npm install --silent 2>&1; then
        fail "vscode"
        err "npm install failed"
        return
    fi

    # Compile
    if ! npm run compile 2>&1; then
        fail "vscode"
        err "TypeScript compilation failed"
        return
    fi

    # Test
    if ! npm test 2>&1; then
        fail "vscode"
        err "Tests failed"
        return
    fi

    pass "vscode"
}

#=============================================================================
# Visual Studio Extension
#=============================================================================
build_vs() {
    log "Building Visual Studio extension..."
    cd "$SCRIPT_DIR/vs-dtt"

    if ! has_cmd dotnet; then
        skip "vs"
        warn "dotnet not found"
        return
    fi

    local output
    output=$(dotnet build --nologo -v q 2>&1)
    local exit_code=$?

    if [[ $exit_code -ne 0 ]]; then
        fail "vs"
        err "dotnet build failed:"
        echo "$output"
        return
    fi

    # Check for errors (not warnings)
    if echo "$output" | grep -q "error CS"; then
        fail "vs"
        err "Build has errors:"
        echo "$output" | grep "error CS"
        return
    fi

    pass "vs"
}

#=============================================================================
# CLion Plugin
#=============================================================================
build_clion() {
    log "Building CLion plugin..."
    cd "$SCRIPT_DIR/clion-dtt"

    if ! has_cmd java; then
        skip "clion"
        warn "java not found"
        return
    fi

    # Use gradlew if available, otherwise try system gradle
    local gradle_cmd="./gradlew"
    if [[ ! -x "$gradle_cmd" ]]; then
        if has_cmd gradle; then
            log "Bootstrapping Gradle wrapper..."
            gradle wrapper --gradle-version 8.5 2>/dev/null || true
        fi
    fi

    if [[ ! -x "$gradle_cmd" ]]; then
        skip "clion"
        warn "gradle/gradlew not available"
        return
    fi

    if ! $gradle_cmd build --quiet 2>&1; then
        fail "clion"
        err "Gradle build failed"
        return
    fi

    pass "clion"
}

#=============================================================================
# Emacs Package
#=============================================================================
build_emacs() {
    log "Building Emacs package..."
    cd "$SCRIPT_DIR"

    if ! has_cmd emacs; then
        skip "emacs"
        warn "emacs not found"
        return
    fi

    # Install flycheck
    install_flycheck

    # Byte-compile
    local output
    output=$(emacs --batch \
        --eval "(progn (require 'package) (setq package-user-dir \"/tmp/emacs-dtt-pkg\") (package-initialize))" \
        -f batch-byte-compile emacs-dtt/dtt.el 2>&1)
    local exit_code=$?

    # Check for actual errors (not warnings)
    if echo "$output" | grep -q "Error:"; then
        fail "emacs"
        err "Byte-compilation failed:"
        echo "$output" | grep -A2 "Error:"
        return
    fi

    if [[ ! -f "emacs-dtt/dtt.elc" ]]; then
        fail "emacs"
        err "No .elc file produced"
        return
    fi

    pass "emacs"
}

#=============================================================================
# Neovim Plugin
#=============================================================================
build_nvim() {
    log "Validating Neovim plugin..."
    cd "$SCRIPT_DIR"

    if ! has_cmd luac && ! has_cmd luajit; then
        skip "nvim"
        warn "luac/luajit not found"
        return
    fi

    local lua_cmd="luac"
    has_cmd luac || lua_cmd="luajit"

    if ! $lua_cmd -p nvim-dtt/lua/dtt/init.lua 2>&1; then
        fail "nvim"
        err "Lua syntax error"
        return
    fi

    pass "nvim"
}

#=============================================================================
# Vim Plugin
#=============================================================================
build_vim() {
    log "Validating Vim plugin..."
    cd "$SCRIPT_DIR"

    if ! has_cmd vim; then
        skip "vim"
        warn "vim not found"
        return
    fi

    # Source the plugin in ex mode to check for syntax errors
    local output
    output=$(vim -u NONE -e -s -c "source vim-dtt/plugin/dtt.vim" -c "qa!" 2>&1)
    local exit_code=$?

    if [[ $exit_code -ne 0 ]] || [[ -n "$output" ]]; then
        fail "vim"
        err "Vim script error:"
        echo "$output"
        return
    fi

    pass "vim"
}

#=============================================================================
# Main
#=============================================================================
#=============================================================================
# Package all plugins and generate checksums
#=============================================================================
package_all() {
    log "Packaging all plugins..."

    mkdir -p dist

    # VS Code - create .vsix
    cd "$SCRIPT_DIR/vscode-dtt"
    if has_cmd npx && [[ -d node_modules ]]; then
        npx vsce package --allow-missing-repository -o ../dist/dtt-vscode.vsix 2>/dev/null && \
            log "Created dtt-vscode.vsix" || warn "VS Code packaging failed"
    fi

    # CLion - copy .zip from build
    cd "$SCRIPT_DIR/clion-dtt"
    if [[ -d build/distributions ]]; then
        cp build/distributions/*.zip ../dist/ 2>/dev/null && \
            log "Copied CLion plugin zip" || true
    fi

    # Visual Studio - the build output is in bin/
    cd "$SCRIPT_DIR/vs-dtt"
    if [[ -d bin/Release ]] || [[ -d bin/Debug ]]; then
        # Find the vsix or dll
        find bin -name "*.vsix" -exec cp {} ../dist/ \; 2>/dev/null || true
    fi

    # Emacs - copy .el and .elc
    cd "$SCRIPT_DIR"
    if [[ -f emacs-dtt/dtt.elc ]]; then
        cp emacs-dtt/dtt.el emacs-dtt/dtt.elc dist/ 2>/dev/null && \
            log "Copied Emacs package" || true
    fi

    # Neovim - create tarball
    tar -czf dist/dtt-nvim.tar.gz nvim-dtt 2>/dev/null && \
        log "Created dtt-nvim.tar.gz" || true

    # Vim - create tarball
    tar -czf dist/dtt-vim.tar.gz vim-dtt 2>/dev/null && \
        log "Created dtt-vim.tar.gz" || true

    # Generate checksums
    generate_checksums
}

#=============================================================================
# Generate checksums.txt
#=============================================================================
generate_checksums() {
    log "Generating checksums.txt..."
    cd "$SCRIPT_DIR/dist"

    if [[ -z "$(ls -A . 2>/dev/null)" ]]; then
        warn "No artifacts to checksum"
        return
    fi

    # Generate SHA256 checksums
    if has_cmd sha256sum; then
        sha256sum * > checksums.txt 2>/dev/null
    elif has_cmd shasum; then
        shasum -a 256 * > checksums.txt 2>/dev/null
    else
        warn "No sha256sum or shasum available"
        return
    fi

    log "checksums.txt created:"
    cat checksums.txt
    cd "$SCRIPT_DIR"
}

print_results() {
    echo ""
    echo "═══════════════════════════════════════════════════════"
    echo "                    BUILD RESULTS"
    echo "═══════════════════════════════════════════════════════"
    echo -e "  VS Code              ${RESULTS[vscode]:-${YELLOW}NOT RUN${NC}}"
    echo -e "  Visual Studio        ${RESULTS[vs]:-${YELLOW}NOT RUN${NC}}"
    echo -e "  CLion                ${RESULTS[clion]:-${YELLOW}NOT RUN${NC}}"
    echo -e "  Emacs                ${RESULTS[emacs]:-${YELLOW}NOT RUN${NC}}"
    echo -e "  Neovim               ${RESULTS[nvim]:-${YELLOW}NOT RUN${NC}}"
    echo -e "  Vim                  ${RESULTS[vim]:-${YELLOW}NOT RUN${NC}}"
    echo "═══════════════════════════════════════════════════════"

    # Count failures
    local failures=0
    for key in "${!RESULTS[@]}"; do
        if [[ "${RESULTS[$key]}" == *"FAIL"* ]]; then
            ((failures++))
        fi
    done

    if [[ $failures -gt 0 ]]; then
        echo -e "${RED}$failures plugin(s) failed${NC}"
        exit 1
    else
        echo -e "${GREEN}All plugins verified successfully${NC}"
        exit 0
    fi
}

main() {
    local do_clean=false
    local do_install=false
    local do_package=false

    # Parse arguments
    for arg in "$@"; do
        case $arg in
            --clean) do_clean=true ;;
            --install-deps) do_install=true ;;
            --package) do_package=true ;;
            --help|-h)
                echo "Usage: $0 [--clean] [--install-deps] [--package]"
                echo ""
                echo "Options:"
                echo "  --clean        Clean all build artifacts first"
                echo "  --install-deps Install required dependencies (apt-get)"
                echo "  --package      Create distributable packages and checksums"
                exit 0
                ;;
        esac
    done

    echo "═══════════════════════════════════════════════════════"
    echo "          DTT IDE Plugins - Build & Verify"
    echo "═══════════════════════════════════════════════════════"
    echo ""

    # Clean if requested
    if $do_clean; then
        clean_all
        echo ""
    fi

    # Install deps if requested
    if $do_install; then
        install_deps
        echo ""
    fi

    # Build all plugins (continue on failure to get full report)
    build_vscode || true
    echo ""
    build_vs || true
    echo ""
    build_clion || true
    echo ""
    build_emacs || true
    echo ""
    build_nvim || true
    echo ""
    build_vim || true

    # Package if requested
    if $do_package; then
        echo ""
        package_all
    fi

    print_results
}

main "$@"
