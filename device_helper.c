#include "ota_coordinator.h"
#define FSTAB_PATH "/vendor/etc/fstab.aaos_iasw" //todo: different platform?
#define FSTAB_PATH_RECOVERY "/etc/recovery.fstab"
#define RECOVERY_CMD "boot-recovery"
#define RECOVERY_OTA_UPDATE "recovery\n--update_package=/mota/aaos_iasw-ota-eng.jade.zip\n--dont_reboot\n--virtiofs"
#define RECOVERY_WIPE_DATA "recovery\n--wipe_data\n--dont_reboot"
#define MISC_PART "/misc"
#define MISC_LABEL "misc"

const size_t BOOTLOADER_MESSAGE_OFFSET_IN_MISC = 0;
const size_t VENDOR_SPACE_OFFSET_IN_MISC = 2 * 1024;
const size_t WIPE_PACKAGE_OFFSET_IN_MISC = 16 * 1024;
 /* Constants.  */
// const char *SLOT_STORAGE_PART = MISC_LABEL;
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*x))
#define MAX_NB_SLOT	ARRAY_SIZE(((struct bootloader_control *)0)->slot_info)
#define MAX_LABEL_LEN	64

// static const int MAX_PRIORITY    = 15;
// static const int MAX_RETRIES     = 7;
// static const char  SUFFIX_FMT[]    = "_%c";
// static const char  SLOT_START_CHAR = 'a';
// static const int SUFFIX_LEN      = 2;

#define SUFFIX_INDEX(suffix) (suffix[1] - SLOT_START_CHAR)

 /* A/B metadata structure. */
typedef struct slot_metadata slot_metadata_t;
typedef struct bootloader_control boot_ctrl_t;

 /* Internal. */
// static int is_used;
// static char _suffixes[MAX_NB_SLOT * sizeof(SUFFIX_FMT)];
// static char *suffixes[MAX_NB_SLOT];
// static char *cur_suffix;	/* Point to one of the suffixes, or
//   				   NULL if there is no active slot. */
// static boot_ctrl_t boot_ctrl;
// static slot_metadata_t *slots = boot_ctrl.slot_info;

int read_misc_partition(void* buf, size_t size, char *misc_blk_device, size_t offset) {
    FILE *file;
    size_t bytesRead;

    file = fopen(misc_blk_device, "r");
    if (file == NULL) {
        log_wrapper(LOG_LEVEL_ERROR, "Failed to open misc file");
        return -1;
    }

    if (fseek(file, offset, SEEK_SET) != 0) {
        log_wrapper(LOG_LEVEL_ERROR, "Failed to seek to the beginning of the file");
        fclose(file);
        return -1;
    }

    bytesRead = fread(buf, sizeof(char), size, file);
    if (bytesRead == 0 && ferror(file)) {
        log_wrapper(LOG_LEVEL_ERROR, "Failed to read file");
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
        log_wrapper(LOG_LEVEL_ERROR, "Failed to open misc file");
        return -1;
    }

    if (lseek(fd, offset, SEEK_SET) != 0) {
        log_wrapper(LOG_LEVEL_ERROR, "Failed to seek to the beginning of the file");
        close(fd);
        return -1;
    }

    bytesWritten = write(fd, buf, size);
    if (bytesWritten != size) {
        log_wrapper(LOG_LEVEL_ERROR, "Failed to write file");
        close(fd);
        return -1;
    }

    if (fsync(fd) == -1) {
        log_wrapper(LOG_LEVEL_ERROR, "Failed to sync data to disk");
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

int get_misc_blk_device(char *misc_blk_device) {
    FILE *file;
    char line[256];
    FstabEntry entry;

    if (IS_RECOVERY) {
        file = fopen(FSTAB_PATH_RECOVERY, "r");
    } else {
        file = fopen(FSTAB_PATH, "r");
    }

    if (file == NULL) {
        log_wrapper(LOG_LEVEL_ERROR, "Failed to open fstab file");
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

        if (strncmp(entry.mount_point, MISC_PART, sizeof(MISC_PART)) == 0) {
            strlcpy(misc_blk_device, entry.device, sizeof(entry.device));
            return 0;
        }
    }
    fclose(file);
    log_wrapper(LOG_LEVEL_ERROR, "Couldn't find the misc device\n");
    return -1;
}

int get_bootloader_message(bootloader_message *boot, char* misc_blk_device) {
    return read_misc_partition(boot, sizeof(*boot), misc_blk_device, BOOTLOADER_MESSAGE_OFFSET_IN_MISC);
}

int get_bootloader_message_ab(bootloader_message_ab *boot_ab, char* misc_blk_device) {
    return read_misc_partition(boot_ab, sizeof(*boot_ab), misc_blk_device, BOOTLOADER_MESSAGE_OFFSET_IN_MISC);
}

int write_bootloader_message(bootloader_message *boot, char* misc_blk_device) {
    return write_misc_partition(boot, sizeof(*boot), misc_blk_device, BOOTLOADER_MESSAGE_OFFSET_IN_MISC);
}

int write_bootloader_message_ab(bootloader_message_ab *boot_ab, char* misc_blk_device) {
    return write_misc_partition(boot_ab, sizeof(*boot_ab), misc_blk_device, BOOTLOADER_MESSAGE_OFFSET_IN_MISC);
}

int write_data_to_bcb(enum OPERATION operation) {
    char misc_blk_device[256];
    bootloader_message boot;
    char log[256];

    if (get_misc_blk_device(misc_blk_device)==-1) {
        log_wrapper(LOG_LEVEL_ERROR, "Failed to get_misc_blk_device\n");
        return -1;
    }
    snprintf(log, sizeof(log), "misc_blk_device: %s\n", misc_blk_device);
    log_wrapper(LOG_LEVEL_INFO, log);

    if (get_bootloader_message(&boot, misc_blk_device)==-1) {
        log_wrapper(LOG_LEVEL_ERROR, "Failed to get_misc_blk_device\n");
        return -1;
    }
    snprintf(log, sizeof(log), "<get before bebootloader message>\nboot.command: %s, boot.status: %s,\
    boot.recovery: %s, boot.stage: %s\n\n",
    boot.command, boot.status, boot.recovery, boot.stage);
    log_wrapper(LOG_LEVEL_INFO, log);

    // if (!wait_for_device(misc_blk_device, err)) {
    //     return false;
    // }

    // Update the boot command field if it's empty, and preserve
    // the other arguments in the bootloader message.
    if (is_boot_cmd_empty(&boot)) {
        log_wrapper(LOG_LEVEL_INFO, "boot.command is empty, write recovery into it.\n");

        if (operation == OTA_UPDATE) {
            strlcpy(boot.command, RECOVERY_CMD, sizeof(RECOVERY_CMD));
            strlcpy(boot.recovery, RECOVERY_OTA_UPDATE, sizeof(RECOVERY_OTA_UPDATE));
        } else if (operation == WIPE_DATA) {
            strlcpy(boot.command, RECOVERY_CMD, sizeof(RECOVERY_CMD));
            strlcpy(boot.recovery, RECOVERY_WIPE_DATA, sizeof(RECOVERY_WIPE_DATA));
        }
        if (write_bootloader_message(&boot, misc_blk_device)==-1) {
            log_wrapper(LOG_LEVEL_ERROR, "Failed to set bootloader message\n");
            return -1;
        }
        // check and log
        if (get_bootloader_message(&boot, misc_blk_device)==-1) {
            log_wrapper(LOG_LEVEL_ERROR, "Failed to get_misc_blk_device\n");
            return -1;
        }
        snprintf(log, sizeof(log), "<get after bebootloader message>\nboot.command: %s, boot.status: %s,\
        boot.recovery: %s, boot.stage: %s\n\n",
        boot.command, boot.status, boot.recovery, boot.stage);
        log_wrapper(LOG_LEVEL_INFO, log);
    } else {
        log_wrapper(LOG_LEVEL_INFO, "boot.command is not empty, don't write recovery into it.\n");
    }
    return 0;
}