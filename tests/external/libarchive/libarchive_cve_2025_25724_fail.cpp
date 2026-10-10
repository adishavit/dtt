// CVE-2025-25724: Buffer overflow from operations on closed archive (should FAIL to compile)
//
// CVE-2025-25724 is a buffer overflow triggered by performing read operations
// on an archive that has already been closed. DTT catches this because
// archive_read_next_header requires @archive_state >= 3, but after close
// the state is -1.
//
// Reference: https://nvd.nist.gov/vuln/detail/CVE-2025-25724

#include <archive.h>
#include <archive_entry.h>

// Variant 1: next_header after close
void variant_read_header_after_close() {
    struct archive *a = archive_read_new();           // @archive_state = 1
    if (!a) return;
    archive_read_support_filter_all(a);               // @archive_state = 2
    archive_read_open_filename(a, "test.tar", 10240); // @archive_state = 3
    archive_read_close(a);                            // @archive_state = -1

    struct archive_entry *entry;
    // Requires @archive_state >= 3; this call must be rejected in enforce mode.
    archive_read_next_header(a, &entry); // dtt-error {{requires @archive_state >= 3}}

    archive_read_free(a);                             // @archive_state = 0
}

// Variant 2: read_data after close
void variant_read_data_after_close() {
    struct archive *a = archive_read_new();           // @archive_state = 1
    if (!a) return;
    archive_read_support_format_all(a);               // @archive_state = 2
    archive_read_open_filename(a, "test.tar", 10240); // @archive_state = 3
    archive_read_close(a);                            // @archive_state = -1

    char buf[1024];
    // Requires @archive_state >= 3; this call must be rejected in enforce mode.
    archive_read_data(a, buf, sizeof(buf)); // dtt-error {{requires @archive_state >= 3}}
    (void)buf;

    archive_read_free(a);                             // @archive_state = 0
}

int main() {
    variant_read_header_after_close();
    variant_read_data_after_close();
    return 0;
}
