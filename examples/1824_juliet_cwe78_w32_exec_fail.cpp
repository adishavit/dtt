// 1824_juliet_cwe78_w32_exec_fail.cpp
// Juliet CWE-78 pattern: Windows exec/spawn sinks with tainted data
// Tests Windows-specific sink families used in Juliet test suite.
//
// Note: Juliet's execv/spawnv patterns pass tainted data through argv arrays
// (aggregate taint propagation), which DTT doesn't track yet. This test covers
// the direct-argument patterns: _spawnl/_spawnlp (variadic) and _execv/_spawnv
// with tainted path.

extern "C" {
    char *getenv(const char *);
    unsigned long strlen(const char *);
    char *strncat(char *, const char *, unsigned long);

    // Windows process execution functions
    int _execv(const char *, const char *const *);
    int _execvp(const char *, const char *const *);
    int _spawnl(int, const char *, const char *, ...);
    int _spawnlp(int, const char *, const char *, ...);
    int _spawnv(int, const char *, const char *const *);
    int _spawnvp(int, const char *, const char *const *);
}

#define P_WAIT 0

// Variant 1: getenv → _spawnl (Juliet w32spawnl) — variadic, data as direct arg
void juliet_getenv_w32_spawnl() {
    char data_buf[100] = "ls ";
    char *data = data_buf;
    unsigned long dataLen = strlen(data);
    char *env = getenv("ADD");
    if (env) {
        strncat(data + dataLen, env, 100 - dataLen - 1);
    }
    _spawnl(P_WAIT, "/bin/sh", "/bin/sh", "-c", data, (char *)0);  // ERROR: tainted arg
}

// Variant 2: getenv → _spawnlp (Juliet w32_spawnlp) — variadic, data as direct arg
void juliet_getenv_w32_spawnlp() {
    char data_buf[100] = "ls ";
    char *data = data_buf;
    unsigned long dataLen = strlen(data);
    char *env = getenv("ADD");
    if (env) {
        strncat(data + dataLen, env, 100 - dataLen - 1);
    }
    _spawnlp(P_WAIT, "sh", "sh", "-c", data, (char *)0);  // ERROR: tainted arg
}

// Variant 3: getenv → _execv with tainted path (direct arg0 sink)
void juliet_getenv_w32_execv_path() {
    char *path = getenv("EVIL_PATH");
    if (path) {
        const char *args[] = {"sh", (char *)0};
        _execv(path, args);   // ERROR: tainted path
    }
}

// Variant 4: getenv → _spawnv with tainted path (direct arg1 sink)
void juliet_getenv_w32_spawnv_path() {
    char *path = getenv("EVIL_PATH");
    if (path) {
        const char *args[] = {"sh", (char *)0};
        _spawnv(P_WAIT, path, args);   // ERROR: tainted path
    }
}

int main() { return 0; }
