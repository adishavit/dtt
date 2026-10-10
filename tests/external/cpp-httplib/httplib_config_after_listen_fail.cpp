// httplib::Server — configuration after listen (should NOT compile)
//
// Tests that DTT catches server configuration methods called after listen().
// All configuration methods require @server_state == ServerState.VALID,
// but listen() transitions to RUNNING.

#include <httplib.h>

// Variant 1: set_write_timeout after listen
void variant_write_timeout_after_listen() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.listen("0.0.0.0", 8080);  // @server_state = ServerState.RUNNING

    svr.set_write_timeout(10, 0);  // DTT ERROR: requires VALID, but is RUNNING  dtt-error {{requires @server_state}}

    svr.stop();
}

// Variant 2: set_payload_max_length after listen
void variant_payload_after_listen() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.listen("0.0.0.0", 8081);  // @server_state = ServerState.RUNNING

    svr.set_payload_max_length(1024);  // DTT ERROR: requires VALID, but is RUNNING  dtt-error {{requires @server_state}}

    svr.stop();
}

// Variant 3: set_idle_interval after listen
void variant_idle_after_listen() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.listen("0.0.0.0", 8082);  // @server_state = ServerState.RUNNING

    svr.set_idle_interval(30, 0);  // DTT ERROR: requires VALID, but is RUNNING  dtt-error {{requires @server_state}}

    svr.stop();
}

// Variant 4: set_keep_alive_max_count after listen
void variant_keepalive_after_listen() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.listen("0.0.0.0", 8083);  // @server_state = ServerState.RUNNING

    svr.set_keep_alive_max_count(50);  // DTT ERROR: requires VALID, but is RUNNING  dtt-error {{requires @server_state}}

    svr.stop();
}

// Variant 5: set_mount_point after listen
void variant_mount_after_listen() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.listen("0.0.0.0", 8084);  // @server_state = ServerState.RUNNING

    svr.set_mount_point("/static", "/var/www");  // KNOWN GAP (not asserted): this overload lacks a state diagnostic.

    svr.stop();
}

// Variant 6: set_base_dir after listen
void variant_basedir_after_listen() {
    httplib::Server svr;  // @server_state = ServerState.VALID

    svr.listen("0.0.0.0", 8085);  // @server_state = ServerState.RUNNING

    svr.set_base_dir("/var/www");  // DTT ERROR: requires VALID, but is RUNNING  dtt-error {{requires @server_state}}

    svr.stop();
}

int main() {
    variant_write_timeout_after_listen();
    variant_payload_after_listen();
    variant_idle_after_listen();
    variant_keepalive_after_listen();
    variant_mount_after_listen();
    variant_basedir_after_listen();
    return 0;
}
