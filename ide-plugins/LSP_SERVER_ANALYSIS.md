# DTT Language Server Protocol (LSP) Analysis

Deep investigation into building an LSP server for real-time DTT diagnostics.

## What is LSP?

The [Language Server Protocol](https://microsoft.github.io/language-server-protocol/) defines a standard JSON-RPC protocol between IDEs and language servers. Once you build an LSP server, it works with:

- VS Code
- Visual Studio
- Neovim/Vim
- Emacs
- Sublime Text
- CLion
- Any LSP-compatible editor

**Key benefit:** Build once, works everywhere.

---

## How LSP Diagnostics Work

```
┌─────────────┐                      ┌─────────────────┐
│   Editor    │  textDocument/       │  Language       │
│   (Client)  │  didOpen/didChange   │  Server         │
│             │ ──────────────────►  │                 │
│             │                      │  1. Receive     │
│             │                      │  2. Analyze     │
│             │  publishDiagnostics  │  3. Parse       │
│             │ ◄──────────────────  │                 │
│  ~~squiggles~~                     │                 │
└─────────────┘                      └─────────────────┘
```

**Flow:**
1. Client sends `textDocument/didOpen` when file opens
2. Client sends `textDocument/didChange` on every edit
3. Server analyzes and sends `textDocument/publishDiagnostics`
4. Client displays squiggles under errors

---

## Option A: diagnostic-languageserver (Easiest)

**What:** A generic LSP server that wraps any command-line linter.

**Repository:** [iamcco/diagnostic-languageserver](https://github.com/iamcco/diagnostic-languageserver)

### How It Works

1. Install: `npm install -g diagnostic-languageserver`
2. Configure to run DTT via clang
3. It parses clang's output using regex
4. Sends diagnostics to editor

### Configuration for DTT

```json
{
  "linters": {
    "dtt": {
      "command": "clang++",
      "args": [
        "-fplugin=/path/to/libDTTPlugin.so",
        "-std=c++20",
        "-fsyntax-only",
        "-fno-color-diagnostics",
        "%file"
      ],
      "rootPatterns": [".git", "compile_commands.json", "CMakeLists.txt"],
      "isStderr": true,
      "debounce": 500,
      "offsetLine": 0,
      "offsetColumn": 0,
      "sourceName": "DTT",
      "formatPattern": [
        "^([^:]+):(\\d+):(\\d+):\\s+(error|warning|note):\\s+(.*)$",
        {
          "sourceName": 1,
          "sourceNameFilter": true,
          "line": 2,
          "column": 3,
          "security": 4,
          "message": 5
        }
      ],
      "securities": {
        "error": "error",
        "warning": "warning",
        "note": "info"
      }
    }
  },
  "filetypes": {
    "cpp": "dtt",
    "c": "dtt"
  }
}
```

### VS Code Client Setup

```json
// settings.json
{
  "diagnostic-languageserver.linters": { /* above config */ },
  "diagnostic-languageserver.filetypes": {
    "cpp": "dtt"
  }
}
```

### Pros & Cons

| Pros | Cons |
|------|------|
| Already exists, just configure | No compile_commands.json support |
| Works with any editor | Must manage include paths manually |
| Minimal development effort | Limited customization |
| Debouncing built-in | Can't add DTT-specific features |

### Effort: **Low** (days)

---

## Option B: Custom Node.js/TypeScript LSP Server

**What:** Build a custom LSP server using VS Code's LSP libraries.

**Reference:** [VS Code Language Server Guide](https://code.visualstudio.com/api/language-extensions/language-server-extension-guide)

### Architecture

```
┌─────────────────────────────────────────────────────────┐
│                  DTT Language Server                     │
├─────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐ │
│  │ LSP Handler │  │ File Watcher│  │ Diagnostic Cache│ │
│  │             │  │             │  │                 │ │
│  │ didOpen     │  │ debounce    │  │ per-file cache  │ │
│  │ didChange   │  │ coalesce    │  │ invalidation    │ │
│  │ didSave     │  │             │  │                 │ │
│  └──────┬──────┘  └──────┬──────┘  └────────┬────────┘ │
│         │                │                   │          │
│         └────────────────┼───────────────────┘          │
│                          ▼                              │
│  ┌─────────────────────────────────────────────────┐   │
│  │              Analysis Engine                     │   │
│  │                                                  │   │
│  │  1. Read compile_commands.json                  │   │
│  │  2. Spawn: clang++ -fplugin=DTT ...             │   │
│  │  3. Parse stderr diagnostics                    │   │
│  │  4. Convert to LSP Diagnostic[]                 │   │
│  └─────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

### Key Components

#### 1. Server Entry Point

```typescript
import {
  createConnection,
  TextDocuments,
  ProposedFeatures,
  InitializeParams,
  TextDocumentSyncKind,
} from 'vscode-languageserver/node';
import { TextDocument } from 'vscode-languageserver-textdocument';

const connection = createConnection(ProposedFeatures.all);
const documents = new TextDocuments(TextDocument);

connection.onInitialize((params: InitializeParams) => {
  return {
    capabilities: {
      textDocumentSync: TextDocumentSyncKind.Incremental,
      diagnosticProvider: {
        interFileDependencies: false,
        workspaceDiagnostics: false
      }
    }
  };
});

documents.onDidChangeContent(change => {
  validateTextDocument(change.document);
});

documents.listen(connection);
connection.listen();
```

#### 2. Diagnostic Provider

```typescript
import { spawn } from 'child_process';
import { Diagnostic, DiagnosticSeverity } from 'vscode-languageserver/node';

// Debounce map
const pendingValidations = new Map<string, NodeJS.Timeout>();

async function validateTextDocument(document: TextDocument): Promise<void> {
  const uri = document.uri;

  // Debounce: wait 500ms after last change
  const existing = pendingValidations.get(uri);
  if (existing) clearTimeout(existing);

  pendingValidations.set(uri, setTimeout(async () => {
    pendingValidations.delete(uri);
    const diagnostics = await runDTTAnalysis(document);
    connection.sendDiagnostics({ uri, diagnostics });
  }, 500));
}

async function runDTTAnalysis(document: TextDocument): Promise<Diagnostic[]> {
  const filePath = fileURLToPath(document.uri);
  const flags = await getCompileFlags(filePath);  // From compile_commands.json

  const args = [
    `-fplugin=${config.pluginPath}`,
    '-fsyntax-only',
    '-fno-color-diagnostics',
    ...flags,
    filePath
  ];

  return new Promise((resolve) => {
    const proc = spawn(config.clangPath, args);
    let stderr = '';

    proc.stderr.on('data', (data) => { stderr += data; });
    proc.on('close', () => {
      resolve(parseDiagnostics(stderr, filePath));
    });
  });
}
```

#### 3. Diagnostic Parser

```typescript
function parseDiagnostics(output: string, currentFile: string): Diagnostic[] {
  const diagnostics: Diagnostic[] = [];
  const regex = /^(.+?):(\d+):(\d+):\s+(error|warning|note):\s+(.+)$/gm;

  let match;
  while ((match = regex.exec(output)) !== null) {
    const [, file, line, col, severity, message] = match;

    // Only include diagnostics for current file
    if (!file.endsWith(path.basename(currentFile))) continue;

    // Skip non-DTT diagnostics
    if (!message.includes('DTT:')) continue;

    diagnostics.push({
      range: {
        start: { line: parseInt(line) - 1, character: parseInt(col) - 1 },
        end: { line: parseInt(line) - 1, character: parseInt(col) }
      },
      severity: mapSeverity(severity),
      source: 'DTT',
      message: message
    });
  }

  return diagnostics;
}
```

#### 4. compile_commands.json Support

```typescript
import * as fs from 'fs';
import * as path from 'path';

interface CompileCommand {
  directory: string;
  file: string;
  command?: string;
  arguments?: string[];
}

let compileCommands: Map<string, CompileCommand> = new Map();

function loadCompileCommands(workspaceRoot: string): void {
  const ccPath = path.join(workspaceRoot, 'build', 'compile_commands.json');
  if (!fs.existsSync(ccPath)) return;

  const commands: CompileCommand[] = JSON.parse(fs.readFileSync(ccPath, 'utf8'));
  for (const cmd of commands) {
    const normalizedFile = path.resolve(cmd.directory, cmd.file);
    compileCommands.set(normalizedFile, cmd);
  }
}

function getCompileFlags(filePath: string): string[] {
  const cmd = compileCommands.get(path.resolve(filePath));
  if (!cmd) return [];

  const args = cmd.arguments || cmd.command?.split(' ') || [];
  // Filter out source file and output flags
  return args.filter(arg =>
    !arg.endsWith('.cpp') &&
    !arg.endsWith('.o') &&
    arg !== '-c' &&
    arg !== '-o'
  );
}
```

### Pros & Cons

| Pros | Cons |
|------|------|
| Full control | More development effort |
| compile_commands.json support | Must handle edge cases |
| Can add DTT-specific features | Need to maintain |
| Hover info, code actions possible | |

### Effort: **Medium** (1-2 weeks)

---

## Option C: Custom C++ LSP Server

**What:** Build the LSP server in C++ for tighter Clang integration.

**Libraries:**
- [lsp-framework](https://github.com/leon-bckl/lsp-framework) - Modern C++20, no dependencies
- [LspCpp](https://github.com/kuafuwang/LspCpp) - Mature, more dependencies

### Why C++?

1. **Direct Clang API access** - Could potentially run DTT in-process
2. **No subprocess overhead** - Faster analysis
3. **Share code with DTT** - Reuse parsing logic
4. **Better performance** - Important for large codebases

### Architecture (Inspired by clangd)

```
┌────────────────────────────────────────────────────────────────┐
│                    DTT Language Server (C++)                    │
├────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                    LSP Transport Layer                   │   │
│  │              (lsp-framework / LspCpp)                    │   │
│  │         stdio / TCP socket communication                 │   │
│  └─────────────────────────────────────────────────────────┘   │
│                              │                                  │
│                              ▼                                  │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                    Request Scheduler                     │   │
│  │                                                          │   │
│  │  • Queue management (per-file)                          │   │
│  │  • Debouncing (500ms after typing stops)                │   │
│  │  • Cancellation (new edit cancels pending analysis)     │   │
│  │  • Priority (foreground file > background)              │   │
│  └─────────────────────────────────────────────────────────┘   │
│                              │                                  │
│                              ▼                                  │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                    Analysis Workers                      │   │
│  │                    (Thread Pool)                         │   │
│  │                                                          │   │
│  │  Worker 1: ┌──────────────────────────────────────────┐ │   │
│  │            │ 1. Load compile_commands.json flags      │ │   │
│  │            │ 2. Create CompilerInstance               │ │   │
│  │            │ 3. Load DTT plugin                       │ │   │
│  │            │ 4. Run FrontendAction                    │ │   │
│  │            │ 5. Collect diagnostics                   │ │   │
│  │            └──────────────────────────────────────────┘ │   │
│  │                                                          │   │
│  │  Worker 2: [Similar]                                    │   │
│  │  Worker N: [Similar]                                    │   │
│  └─────────────────────────────────────────────────────────┘   │
│                              │                                  │
│                              ▼                                  │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                  Diagnostic Store                        │   │
│  │                                                          │   │
│  │  • Per-file diagnostic cache                            │   │
│  │  • Diff detection (only send changes)                   │   │
│  │  • Note/error relationship tracking                     │   │
│  └─────────────────────────────────────────────────────────┘   │
│                                                                 │
└────────────────────────────────────────────────────────────────┘
```

### Key Implementation Details (from clangd)

**Debouncing (from clangd design):**
> "For files that rebuild relatively slowly, starting to build as soon as the first change is seen isn't ideal. If a user quickly types `foo();`, building after the 'f' is typed means there will always be a diagnostic 'unknown identifier f', which is annoying."
>
> "Writes are debounced: rebuilding doesn't start until either a read is received or a short deadline expires (user stopped typing)."

**Threading Model:**
- One ASTWorker per open file
- Queue of operations per worker
- Single thread per file ensures sequential consistency
- Thread pool for parallel analysis of different files

**Diagnostic Flow:**
> "During parsing, clang emits diagnostics through a DiagnosticConsumer callback. clangd's StoreDiags implementation converts them into Diag objects. TUScheduler has logic to determine when a ParsedAST's diagnostics are 'correct enough' to emit."

### Skeleton Implementation

```cpp
#include <lsp/connection.h>
#include <lsp/messagehandler.h>
#include <lsp/messages.h>
#include <thread>
#include <queue>
#include <mutex>

class DTTLanguageServer {
public:
    void run() {
        auto stream = lsp::StdioStream::create();
        lsp::Connection conn(std::move(stream));

        lsp::MessageHandler handler;
        handler
            .add<lsp::InitializeRequest>([this](auto& req) {
                return handleInitialize(req);
            })
            .add<lsp::DidOpenTextDocumentNotification>([this](auto& notif) {
                scheduleAnalysis(notif.params.textDocument.uri);
            })
            .add<lsp::DidChangeTextDocumentNotification>([this](auto& notif) {
                scheduleAnalysis(notif.params.textDocument.uri);
            })
            .add<lsp::DidSaveTextDocumentNotification>([this](auto& notif) {
                scheduleAnalysis(notif.params.textDocument.uri);
            });

        while (conn.processIncomingMessages(handler)) {
            // Process outgoing diagnostics
            publishPendingDiagnostics(conn);
        }
    }

private:
    void scheduleAnalysis(const std::string& uri) {
        std::lock_guard lock(mutex_);
        // Cancel pending analysis for this file
        // Schedule new analysis with debounce
        pendingAnalysis_[uri] = std::chrono::steady_clock::now() +
                                std::chrono::milliseconds(500);
    }

    void analyzeFile(const std::string& uri) {
        // 1. Get compile flags from compile_commands.json
        // 2. Run clang with DTT plugin
        // 3. Parse diagnostics
        // 4. Queue for publishing
    }

    std::mutex mutex_;
    std::map<std::string, std::chrono::time_point<std::chrono::steady_clock>> pendingAnalysis_;
    std::map<std::string, std::vector<lsp::Diagnostic>> diagnosticCache_;
};
```

### Pros & Cons

| Pros | Cons |
|------|------|
| Best performance | Highest development effort |
| Direct Clang integration | C++ complexity |
| Could run DTT in-process | Build system complexity |
| Professional quality | Longer development time |

### Effort: **High** (3-6 weeks)

---

## Performance Considerations

### Analysis Time

DTT runs full Clang parsing + custom analysis. Expected times:
- Small file (100 LOC): ~100-500ms
- Medium file (1000 LOC): ~500ms-2s
- Large file (10000 LOC): ~2-10s

### Strategies for Responsiveness

1. **Debouncing** - Wait 300-500ms after typing stops
2. **Cancellation** - Cancel analysis if file changes
3. **Incremental** - Only re-analyze changed files
4. **Background** - Run analysis in worker threads
5. **Caching** - Cache results, invalidate on change
6. **Preamble** - Precompile headers (like clangd)

### Preamble Optimization (Advanced)

clangd precompiles the "preamble" (everything before the main code):
- `#include` directives
- Preprocessor definitions
- System headers

This is cached and reused, making re-analysis much faster.

---

## Feature Comparison

| Feature | diagnostic-ls | Node.js | C++ |
|---------|--------------|---------|-----|
| Development time | Days | 1-2 weeks | 3-6 weeks |
| compile_commands.json | ❌ | ✅ | ✅ |
| Debouncing | ✅ | ✅ | ✅ |
| Real-time squiggles | ✅ | ✅ | ✅ |
| Custom hover info | ❌ | ✅ | ✅ |
| Code actions (fixes) | ❌ | ✅ | ✅ |
| Performance | Good | Good | Best |
| Preamble caching | ❌ | ❌ | ✅ |
| Maintenance | Low | Medium | High |

---

## Recommendation

### For Quick Validation
**Use diagnostic-languageserver** to prove the concept works. Configure it to run DTT and see if real-time diagnostics are useful.

### For Production Use
**Build a Node.js/TypeScript LSP server:**
- Best effort/quality tradeoff
- compile_commands.json support
- Can add DTT-specific features later
- Maintainable codebase

### For Maximum Quality
**Build a C++ LSP server** if:
- Performance is critical
- You want in-process DTT integration
- You plan to add advanced features (preamble caching, etc.)

---

## Next Steps

1. **Try diagnostic-languageserver** - Quick proof of concept
2. **Prototype Node.js server** - If PoC works, build proper server
3. **Add compile_commands.json** - Essential for real projects
4. **Add code actions** - Suggest fixes (add guards, null checks)
5. **Add hover info** - Show DTT preconditions on hover

---

## References

- [LSP Specification 3.17](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/)
- [VS Code LSP Guide](https://code.visualstudio.com/api/language-extensions/language-server-extension-guide)
- [clangd Design](https://clangd.llvm.org/design/)
- [clangd Threading](https://clangd.llvm.org/design/threads)
- [diagnostic-languageserver](https://github.com/iamcco/diagnostic-languageserver)
- [lsp-framework (C++)](https://github.com/leon-bckl/lsp-framework)
- [LspCpp](https://github.com/kuafuwang/LspCpp)
