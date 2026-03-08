// 1823_juliet_cwe78_ptr_arith_fail.cpp
// Juliet CWE-78 pattern: taint through strncat with pointer arithmetic
// This pattern caused 0% detection before fixing pointer arithmetic unwrapping.

extern "C" {
    char *getenv(const char *);
    unsigned long strlen(const char *);
    char *strncat(char *, const char *, unsigned long);
    int system(const char *);
    char *fgets(char *, int, void *);
    int recv(int, void *, unsigned long, int);
    int execl(const char *, const char *, ...);
}

extern void *stdin;

// Variant 1: getenv → strncat(data+offset, ...) → system (Juliet environment_system)
void juliet_getenv_strncat_system() {
    char data_buf[100] = "ls ";
    char *data = data_buf;
    unsigned long dataLen = strlen(data);
    char *env = getenv("ADD");
    if (env) {
        strncat(data + dataLen, env, 100 - dataLen - 1);  // taint flows to data
    }
    system(data);   // ERROR: tainted shell command (CWE-78)
}

// Variant 2: fgets (stdin) → strncat → system (Juliet console_system)
void juliet_console_strncat_system() {
    char data_buf[100] = "ls ";
    char *data = data_buf;
    unsigned long dataLen = strlen(data);
    if (100 - dataLen > 1) {
        fgets(data + dataLen, (int)(100 - dataLen), stdin);  // taint from stdin
    }
    system(data);   // ERROR: tainted shell command (CWE-78)
}

// Variant 3: recv → strncat → execl arg (Juliet connect_socket_execl)
void juliet_socket_strncat_execl(int sock) {
    char data_buf[100] = "ls ";
    char *data = data_buf;
    unsigned long dataLen = strlen(data);
    recv(sock, (char *)(data + dataLen), 100 - dataLen - 1, 0);
    execl("/bin/sh", "/bin/sh", "-c", data, (char *)0);  // ERROR: tainted arg
}

int main() { return 0; }
