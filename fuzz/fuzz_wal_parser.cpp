#include "indexing/btree.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 10 || size > 1024 * 1024) {
        return 0;
    }

    char wal_path[64];
    char snapshot_path[80];
    snprintf(wal_path, sizeof(wal_path), "/tmp/fuzz_wal_test_%d.wal", static_cast<int>(getpid()));
    snprintf(snapshot_path, sizeof(snapshot_path), "/tmp/fuzz_wal_test_%d.wal.snapshot",
             static_cast<int>(getpid()));

    int fd = open(wal_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        return 0;
    }

    ssize_t written = write(fd, data, size);
    close(fd);

    if (written != static_cast<ssize_t>(size)) {
        unlink(wal_path);
        return 0;
    }

    {
        embrace::indexing::Btree db(wal_path);
        (void)db.recover_from_wal();

        for (int i = 0; i < 5; ++i) {
            (void)db.get("key_" + std::to_string(i));
        }
    }

    unlink(wal_path);
    unlink(snapshot_path);

    return 0;
}
