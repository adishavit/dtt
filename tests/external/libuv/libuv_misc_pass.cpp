// libuv miscellaneous utilities -- correct usage patterns (should COMPILE)
//
// Tests IP address functions, OS utilities, error handling, system info,
// buffer functions, and other stateless/global annotated functions.

#include <uv.h>

// Variant 1: IP address conversion functions
void variant_ip_addr_functions() {
    struct sockaddr_in addr4;
    struct sockaddr_in6 addr6;
    char dst[64];

    uv_ip4_addr("127.0.0.1", 8080, &addr4);                      // requires arg0, arg2 _nonnull
    uv_ip6_addr("::1", 8080, &addr6);                             // requires arg0, arg2 _nonnull

    uv_ip4_name(&addr4, dst, sizeof(dst));                         // requires arg0, arg1 _nonnull
    uv_ip6_name(&addr6, dst, sizeof(dst));                         // requires arg0, arg1 _nonnull
    uv_ip_name((const struct sockaddr*)&addr4, dst, sizeof(dst));  // requires arg0, arg1 _nonnull
}

// Variant 2: inet conversion functions
void variant_inet_functions() {
    char dst[64];
    unsigned char src[16];

    uv_inet_ntop(AF_INET, src, dst, sizeof(dst));                 // requires arg1, arg2 _nonnull
    uv_inet_pton(AF_INET, "127.0.0.1", src);                      // requires arg1, arg2 _nonnull
}

// Variant 3: uv_buf_init
void variant_buf_init() {
    char data[128];
    uv_buf_t buf = uv_buf_init(data, sizeof(data));               // requires arg0 _nonnull
    (void)buf;
}

// Variant 4: working directory functions
void variant_cwd_chdir() {
    char buf[256];
    size_t len = sizeof(buf);

    uv_cwd(buf, &len);                                            // requires arg0, arg1 _nonnull
    uv_chdir("/tmp");                                              // requires arg0 _nonnull
}

// Variant 5: OS directory functions
void variant_os_dirs() {
    char buf[256];
    size_t len = sizeof(buf);

    uv_os_homedir(buf, &len);                                     // requires arg0, arg1 _nonnull
    len = sizeof(buf);
    uv_os_tmpdir(buf, &len);                                      // requires arg0, arg1 _nonnull
}

// Variant 6: environment variable functions
void variant_os_env() {
    char buf[256];
    size_t len = sizeof(buf);

    uv_os_getenv("PATH", buf, &len);                              // requires all args _nonnull
    uv_os_setenv("DTT_TEST", "1");                                 // requires arg0, arg1 _nonnull
    uv_os_unsetenv("DTT_TEST");                                   // requires arg0 _nonnull
}

// Variant 7: process ID functions and hrtime
void variant_pid_and_time() {
    uv_os_getpid();                                                // no state requirements
    uv_os_getppid();                                               // no state requirements
    uv_hrtime();                                                   // no state requirements
}

// Variant 8: error handling functions
void variant_error_functions() {
    char buf[256];

    uv_strerror(-1);                                               // no state requirements
    uv_strerror_r(-1, buf, sizeof(buf));                           // requires arg1 _nonnull
    uv_err_name(-1);                                               // no state requirements
    uv_err_name_r(-1, buf, sizeof(buf));                           // requires arg1 _nonnull
    uv_translate_sys_error(1);                                     // no state requirements
}

// Variant 9: memory and system info functions
void variant_system_info() {
    size_t rss;
    double uptime_val;

    uv_get_free_memory();                                          // no state requirements
    uv_get_total_memory();                                         // no state requirements
    uv_get_constrained_memory();                                   // no state requirements
    uv_get_available_memory();                                     // no state requirements
    uv_resident_set_memory(&rss);                                  // requires arg0 _nonnull
    uv_uptime(&uptime_val);                                        // requires arg0 _nonnull
}

// Variant 10: CPU and interface info
void variant_cpu_interface_info() {
    uv_cpu_info_t* cpu_infos;
    int cpu_count;
    uv_interface_address_t* addresses;
    int addr_count;

    uv_cpu_info(&cpu_infos, &cpu_count);                           // requires arg0, arg1 _nonnull
    uv_free_cpu_info(cpu_infos, cpu_count);                        // requires arg0 _nonnull

    uv_interface_addresses(&addresses, &addr_count);               // requires arg0, arg1 _nonnull
    uv_free_interface_addresses(addresses, addr_count);            // requires arg0 _nonnull
}

// Variant 11: version and type introspection
void variant_version_and_types() {
    uv_version();                                                  // no state requirements
    uv_version_string();                                           // no state requirements
    uv_req_size(UV_WORK);                                          // no state requirements
    uv_req_type_name(UV_WORK);                                    // no state requirements
    uv_handle_size(UV_TCP);                                        // no state requirements
    uv_handle_type_name(UV_TCP);                                  // no state requirements
}

// Variant 12: handle data and introspection functions
void variant_handle_introspection() {
    uv_loop_t loop;
    uv_tcp_t tcp;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_tcp_init(&loop, &tcp);                                      // @handle_state = 1

    uv_handle_get_data((uv_handle_t*)&tcp);                        // requires _nonnull
    uv_handle_set_data((uv_handle_t*)&tcp, nullptr);               // requires _nonnull
    uv_handle_get_loop((uv_handle_t*)&tcp);                        // requires _nonnull
    uv_handle_get_type((uv_handle_t*)&tcp);                        // requires _nonnull
    uv_is_active((uv_handle_t*)&tcp);                              // requires _nonnull
    uv_is_closing((uv_handle_t*)&tcp);                             // requires _nonnull
    uv_has_ref((uv_handle_t*)&tcp);                                // requires _nonnull

    uv_close((uv_handle_t*)&tcp, 0);                              // @handle_state = 3
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

// Variant 13: handle ref/unref and buffer size functions
void variant_handle_ref_unref() {
    uv_loop_t loop;
    uv_tcp_t tcp;
    uv_os_fd_t fd;
    int val = 0;

    uv_loop_init(&loop);                                           // @loop_state = 1
    uv_tcp_init(&loop, &tcp);                                      // @handle_state = 1

    uv_ref((uv_handle_t*)&tcp);                                    // requires @handle_state != 0
    uv_unref((uv_handle_t*)&tcp);                                  // requires @handle_state != 0
    uv_send_buffer_size((uv_handle_t*)&tcp, &val);                 // requires @handle_state != 0
    uv_recv_buffer_size((uv_handle_t*)&tcp, &val);                 // requires @handle_state != 0
    uv_fileno((uv_handle_t*)&tcp, &fd);                            // requires @handle_state != 0

    uv_close((uv_handle_t*)&tcp, 0);
    uv_run(&loop, UV_RUN_NOWAIT);
    uv_loop_close(&loop);
}

int main() {
    variant_ip_addr_functions();
    variant_inet_functions();
    variant_buf_init();
    variant_cwd_chdir();
    variant_os_dirs();
    variant_os_env();
    variant_pid_and_time();
    variant_error_functions();
    variant_system_info();
    variant_cpu_interface_info();
    variant_version_and_types();
    variant_handle_introspection();
    variant_handle_ref_unref();
    return 0;
}
