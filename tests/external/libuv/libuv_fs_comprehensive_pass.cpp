// libuv filesystem operations comprehensive -- correct usage patterns (should COMPILE)
//
// Tests ALL uv_fs_* annotated functions with proper loop state.
// All fs operations require loop.@loop_state == 1 and req._nonnull != 0.
// Uses synchronous mode (NULL callback) throughout.

#include <uv.h>

// Variant 1: basic file I/O workflow: open -> read -> write -> close
void variant_file_io() {
    uv_loop_t loop;
    uv_fs_t req;
    char buf[1024];
    uv_buf_t iov = uv_buf_init(buf, sizeof(buf));

    uv_loop_init(&loop);                                      // @loop_state = 1

    uv_fs_open(&loop, &req, "/tmp/test.txt", 0, 0, 0);       // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_fs_read(&loop, &req, 0, &iov, 1, 0, 0);               // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_fs_write(&loop, &req, 0, &iov, 1, 0, 0);              // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_fs_close(&loop, &req, 0, 0);                           // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_loop_close(&loop);                                     // @loop_state = 3
}

// Variant 2: stat family: stat, fstat, lstat
void variant_stat_family() {
    uv_loop_t loop;
    uv_fs_t req;

    uv_loop_init(&loop);                                      // @loop_state = 1

    uv_fs_stat(&loop, &req, "/tmp", 0);                       // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_fs_fstat(&loop, &req, 0, 0);                           // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_fs_lstat(&loop, &req, "/tmp", 0);                      // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_loop_close(&loop);                                     // @loop_state = 3
}

// Variant 3: directory operations: mkdir, rmdir, opendir, readdir, closedir, scandir
void variant_directory_ops() {
    uv_loop_t loop;
    uv_fs_t req;
    uv_dir_t dir;
    uv_dirent_t ent;

    uv_loop_init(&loop);                                      // @loop_state = 1

    uv_fs_mkdir(&loop, &req, "/tmp/testdir", 0755, 0);        // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_fs_opendir(&loop, &req, "/tmp/testdir", 0);            // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_fs_readdir(&loop, &req, &dir, 0);                      // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_fs_closedir(&loop, &req, &dir, 0);                     // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_fs_scandir(&loop, &req, "/tmp/testdir", 0, 0);         // requires @loop_state == 1
    uv_fs_scandir_next(&req, &ent);                            // requires req._nonnull != 0
    uv_fs_req_cleanup(&req);

    uv_fs_rmdir(&loop, &req, "/tmp/testdir", 0);              // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_loop_close(&loop);                                     // @loop_state = 3
}

// Variant 4: file manipulation: rename, unlink, link, symlink, readlink, realpath
void variant_file_manipulation() {
    uv_loop_t loop;
    uv_fs_t req;

    uv_loop_init(&loop);                                      // @loop_state = 1

    uv_fs_rename(&loop, &req, "/tmp/a.txt", "/tmp/b.txt", 0); // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_fs_unlink(&loop, &req, "/tmp/b.txt", 0);               // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_fs_link(&loop, &req, "/tmp/c.txt", "/tmp/d.txt", 0);   // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_fs_symlink(&loop, &req, "/tmp/c.txt", "/tmp/sym.txt", 0, 0); // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_fs_readlink(&loop, &req, "/tmp/sym.txt", 0);           // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_fs_realpath(&loop, &req, "/tmp/c.txt", 0);             // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_loop_close(&loop);                                     // @loop_state = 3
}

// Variant 5: permissions and ownership: chmod, fchmod, chown, fchown
void variant_permissions() {
    uv_loop_t loop;
    uv_fs_t req;

    uv_loop_init(&loop);                                      // @loop_state = 1

    uv_fs_chmod(&loop, &req, "/tmp/test.txt", 0644, 0);       // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_fs_fchmod(&loop, &req, 0, 0644, 0);                    // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_fs_chown(&loop, &req, "/tmp/test.txt", 0, 0, 0);       // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_fs_fchown(&loop, &req, 0, 0, 0, 0);                    // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_loop_close(&loop);                                     // @loop_state = 3
}

// Variant 6: timestamps and access: utime, futime, access
void variant_timestamps() {
    uv_loop_t loop;
    uv_fs_t req;

    uv_loop_init(&loop);                                      // @loop_state = 1

    uv_fs_utime(&loop, &req, "/tmp/test.txt", 0.0, 0.0, 0);  // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_fs_futime(&loop, &req, 0, 0.0, 0.0, 0);               // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_fs_access(&loop, &req, "/tmp/test.txt", 0, 0);         // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_loop_close(&loop);                                     // @loop_state = 3
}

// Variant 7: temp files and copy: mkdtemp, mkstemp, copyfile, sendfile
void variant_temp_and_copy() {
    uv_loop_t loop;
    uv_fs_t req;

    uv_loop_init(&loop);                                      // @loop_state = 1

    uv_fs_mkdtemp(&loop, &req, "/tmp/dtt_XXXXXX", 0);         // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_fs_mkstemp(&loop, &req, "/tmp/dtt_XXXXXX", 0);         // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_fs_copyfile(&loop, &req, "/tmp/a.txt", "/tmp/b.txt", 0, 0); // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_fs_sendfile(&loop, &req, 1, 0, 0, 1024, 0);           // requires @loop_state == 1
    uv_fs_req_cleanup(&req);

    uv_loop_close(&loop);                                     // @loop_state = 3
}

// Variant 8: full fs workflow: init -> open -> read -> write -> stat -> close -> unlink -> loop_close
void variant_full_fs_workflow() {
    uv_loop_t loop;
    uv_fs_t req;
    char buf[256];
    uv_buf_t iov = uv_buf_init(buf, sizeof(buf));

    uv_loop_init(&loop);                                      // @loop_state = 1

    uv_fs_open(&loop, &req, "/tmp/workflow.txt", 0, 0644, 0);
    uv_fs_req_cleanup(&req);

    uv_fs_read(&loop, &req, 0, &iov, 1, 0, 0);
    uv_fs_req_cleanup(&req);

    uv_fs_write(&loop, &req, 0, &iov, 1, 0, 0);
    uv_fs_req_cleanup(&req);

    uv_fs_fstat(&loop, &req, 0, 0);
    uv_fs_req_cleanup(&req);

    uv_fs_close(&loop, &req, 0, 0);
    uv_fs_req_cleanup(&req);

    uv_fs_unlink(&loop, &req, "/tmp/workflow.txt", 0);
    uv_fs_req_cleanup(&req);

    uv_loop_close(&loop);                                     // @loop_state = 3
}

int main() {
    variant_file_io();
    variant_stat_family();
    variant_directory_ops();
    variant_file_manipulation();
    variant_permissions();
    variant_timestamps();
    variant_temp_and_copy();
    variant_full_fs_workflow();
    return 0;
}
