#include "ota_coordinator.h"
#define FSTAB_PATH "/vendor/etc/fstab.base_aaos" //todo: different platform?


int read_misc_partition(void* buf, size_t size, char *misc_blk_device, size_t offset) {
    FILE *file;
    size_t bytesRead;

    file = fopen(misc_blk_device, "r");
    if (file == NULL) {
        perror("Failed to open misc file");
        return -1;
    }

    if (fseek(file, offset, SEEK_SET) != 0) {
        perror("Failed to seek to the beginning of the file");
        fclose(file);
        return -1;
    }

    bytesRead = fread(buf, sizeof(char), size, file);
    if (bytesRead == 0 && ferror(file)) {
        perror("Failed to read file");
        fclose(file);
        return -1;
    }

    fclose(file);
    return 0;
}


int write_misc_partition(const void* buf, size_t size, char *misc_blk_device, size_t offset) {
    int fd;
    size_t bytesWritten;

    fd = open(misc_blk_device, O_WRONLY);
    if (fd == -1) {
        perror("Failed to open misc file");
        return -1;
    }

    if (lseek(fd, offset, SEEK_SET) != 0) {
        perror("Failed to seek to the beginning of the file");
        close(fd);
        return -1;
    }

    bytesWritten = write(fd, buf, size);
    if (bytesWritten != size) {
        perror("Failed to write file");
        close(fd);
        return -1;
    }

    if (fsync(fd) == -1) {
        perror("Failed to sync data to disk");
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

int get_misc_blk_device(char *misc_blk_device) {
    FILE *file = fopen(FSTAB_PATH, "r");
    char line[256];
    FstabEntry entry;

    if (file == NULL) {
        perror("Failed to open fstab file");
        return -1;
    }

    while (fgets(line, sizeof(line), file)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }

        if (sscanf(line, "%63s %63s %15s %63s",
                   entry.device,
                   entry.mount_point,
                   entry.fs_type,
                   entry.options) != 4) {
            fprintf(stderr, "Malformed fstab entry: %s", line);
            continue;
        }

        if (strncmp(entry.mount_point, "/misc", sizeof("/misc")) == 0) {
            strlcpy(misc_blk_device, entry.device, sizeof(entry.device));
            return 0;
        }
    }
    fclose(file);
    perror("Couldn't find the misc device\n");
    return -1;
}
