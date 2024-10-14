#include "ota_coordinator.h"

#define ANDROID_RB_PROPERTY "sys.powerctl"
#define OTA_PACKAGE "/mota/aaos_iasw-ota-eng.jade.zip"
#define RECOVERY_CMD "boot-recovery"
// #define RECOVERY_PATH "recovery\n--update_package=/udiska/aaos_iasw-ota-eng.jade.zip\n--dont_reboot"
#define RECOVERY_PATH "recovery\n--update_package=/mota/aaos_iasw-ota-eng.jade.zip\n--dont_reboot\n--virtiofs"

void log_wrapper(LogLevel level, const char *log) {
    if (IS_RECOVERY) {
        printf("%s", log);
    } else {
        switch (level) {
            case LOG_LEVEL_DEBUG:
                ALOGD("%s", log);
                break;
            case LOG_LEVEL_INFO:
                ALOGI("%s", log);
                break;
            case LOG_LEVEL_WARNING:
                ALOGW("%s", log);
                break;
            case LOG_LEVEL_ERROR:
                ALOGE("%s", log);
                break;
        }
    }
}

int isprint(int c) {
	return (unsigned)c-0x20 < 0x5f;
}

int is_boot_cmd_empty(bootloader_message* boot) {
    if (boot->command[0] == '\0')
        return 1;

    for (size_t i = 0; i < sizeof(boot->command); ++i) {
        if (boot->command[i] == '\0')
            return 0;
        if (!isprint(boot->command[i]))
            break;
    }

    memset(boot->command, 0, sizeof(boot->command));
    return 1;
}

int write_recovery_to_bcb() {
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
        strlcpy(boot.command, RECOVERY_CMD, sizeof(RECOVERY_CMD));
        strlcpy(boot.recovery, RECOVERY_PATH, sizeof(RECOVERY_PATH));
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

int handle_start_ota(PCI_TTY_Config *config) {
    if (send_message(config->fd_write, "need_to_ota\n")) {
        log_wrapper(LOG_LEVEL_ERROR, "send_message failed!\n");
        return -1;
    }
    return 0;
}

// remove package and drop this update
int handle_ota_package_not_ready(PCI_TTY_Config *config){

    if (config == NULL) {
        printf("");
    }

    if (remove(OTA_PACKAGE) == 0) {
        log_wrapper(LOG_LEVEL_INFO, "OTA_PACKAGE deleted successfully.\n");
    } else {
        log_wrapper(LOG_LEVEL_ERROR, "remove");
        return -1;
    }

    return 0;
}

int shutdown() {
    char log[256];
    int ret = property_set(ANDROID_RB_PROPERTY, "shutdown");

    if (ret < 0) {
        log_wrapper(LOG_LEVEL_ERROR,"Shutdown failed\n");
        return -1;
    } else {
        snprintf(log, sizeof(log), "Script executed with exit status: %d\n", WEXITSTATUS(ret));
        log_wrapper(LOG_LEVEL_INFO, log);
        return 0;
    }
}

//All VMs set the next boot target as recovery, notify SOS, shutdown
int handle_ota_package_ready(PCI_TTY_Config *config) {
    write_recovery_to_bcb();

    //notify SOS
    if (send_message(config->fd_write, "vm_shutdown\n")) {
        log_wrapper(LOG_LEVEL_ERROR, "send_message failed!\n");
        return EXIT_FAILURE;
    }

    return shutdown();
}

// install the package and send the notification to SOS
int handle_start_install(PCI_TTY_Config *config) {
    // todo: start to install the package


    // installed well
    if (send_message(config->fd_write, "successful\n")) {
        log_wrapper(LOG_LEVEL_ERROR, "send_message failed!\n");
        return -1;
    }

    //todo: installed failed, ignore for now
    return 0;
}

int debug_get_slot_info() {
    char misc_blk_device[256];
    bootloader_message_ab boot_ab;
    char log[256];
    struct slot_metadata *slot_info[4]; // Per-slot information.  Up to 4 slots.
    int max_priority = 0, max_index;

    if (get_misc_blk_device(misc_blk_device)==-1) {
        log_wrapper(LOG_LEVEL_ERROR, "Failed to get_misc_blk_device\n");
        return -1;
    }
    snprintf(log, sizeof(log), "misc_blk_device: %s\n", misc_blk_device);
    log_wrapper(LOG_LEVEL_INFO, log);

    if (get_bootloader_message_ab(&boot_ab, misc_blk_device)==-1) {
        log_wrapper(LOG_LEVEL_ERROR, "Failed to get_misc_blk_device\n");
        return -1;
    }
    snprintf(log, sizeof(log), "bootloader_ctrl.magic: 0x%x, boot.nb_slot: %d, \n\n",
    boot_ab.bootloader_ctrl.magic, boot_ab.bootloader_ctrl.nb_slot);
    log_wrapper(LOG_LEVEL_INFO, log);
    // Support there are 2 slots
    // For each slot
    for (int i = 0; i < boot_ab.bootloader_ctrl.nb_slot; ++i) {
        slot_info[i] = &boot_ab.bootloader_ctrl.slot_info[i];
        if (slot_info[i]->priority == 0) {
            snprintf(log, sizeof(log), "slot %d is unbootable! Cannot rollback, return -1", i);
            log_wrapper(LOG_LEVEL_INFO, log);
            return -1;
        }
        if (slot_info[i]->priority >= max_priority) {
            max_index = i;
            max_priority = slot_info[i]->priority;
        }
        snprintf(log, sizeof(log), "slot %d, priority: %d, successful_boot: %d, tries_remaining: %d, verity_corrupted: %d \n",
        i, slot_info[i]->priority,slot_info[i]->successful_boot,slot_info[i]->tries_remaining,slot_info[i]->verity_corrupted);
        log_wrapper(LOG_LEVEL_INFO, log);
    }

    snprintf(log, sizeof(log), "max_index: %d, max_priority: %d \n\n",
    max_index, max_priority);
    log_wrapper(LOG_LEVEL_INFO, log);
    return 0;
}

int debug_mount(){
    if (mount("myfs", "/mota", "virtiofs", 0, NULL) == -1) {
        log_wrapper(LOG_LEVEL_ERROR,"Error mounting myfs on /mota\n");
        return 1;
    }

    log_wrapper(LOG_LEVEL_INFO,"Successfully mounted myfs on /mota\n");
    return 0;
}

int debug_umount() {
    if (umount("/mota") == -1) {
        log_wrapper(LOG_LEVEL_ERROR,"Error umounting myfs on /mota\n");
        return 1;
    }

    log_wrapper(LOG_LEVEL_INFO,"Successfully umounted myfs on /mota\n");
    return 0;
}

int handle_rollback() {
    char misc_blk_device[256];
    bootloader_message_ab boot_ab;
    char log[256];
    struct slot_metadata *slot_info[4]; // Per-slot information.  Up to 4 slots.
    int max_priority = 0, max_index;

    if (get_misc_blk_device(misc_blk_device)==-1) {
        log_wrapper(LOG_LEVEL_ERROR, "Failed to get_misc_blk_device\n");
        return -1;
    }
    snprintf(log, sizeof(log), "misc_blk_device: %s\n", misc_blk_device);
    log_wrapper(LOG_LEVEL_INFO, log);

    if (get_bootloader_message_ab(&boot_ab, misc_blk_device)==-1) {
        log_wrapper(LOG_LEVEL_ERROR, "Failed to get_misc_blk_device\n");
        return -1;
    }
    snprintf(log, sizeof(log), "bootloader_ctrl.magic: 0x%x, boot.nb_slot: %d, \n\n",
    boot_ab.bootloader_ctrl.magic, boot_ab.bootloader_ctrl.nb_slot);
    log_wrapper(LOG_LEVEL_INFO, log);
    // Support there are 2 slots
    // For each slot
    for (int i = 0; i < boot_ab.bootloader_ctrl.nb_slot; ++i) {
        slot_info[i] = &boot_ab.bootloader_ctrl.slot_info[i];
        if (slot_info[i]->priority == 0) {
            snprintf(log, sizeof(log), "slot %d is unbootable! Cannot rollback, return -1", i);
            log_wrapper(LOG_LEVEL_INFO, log);
            return -1;
        }
        if (slot_info[i]->priority >= max_priority) {
            max_index = i;
            max_priority = slot_info[i]->priority;
        }
        snprintf(log, sizeof(log), "<before> slot %d, priority: %d, successful_boot: %d, tries_remaining: %d, verity_corrupted: %d \n",
        i, slot_info[i]->priority,slot_info[i]->successful_boot,slot_info[i]->tries_remaining,slot_info[i]->verity_corrupted);
        log_wrapper(LOG_LEVEL_INFO, log);
    }

    snprintf(log, sizeof(log), "<before> max_index: %d, max_priority: %d \n\n",
    max_index, max_priority);
    log_wrapper(LOG_LEVEL_INFO, log);

    // Rollback and log
    for (int i = 0; i < boot_ab.bootloader_ctrl.nb_slot; ++i) {
        if (i == max_index) {
            slot_info[i]->priority = max_priority-1;
        } else {
            slot_info[i]->priority = max_priority;
            slot_info[i]->tries_remaining = 6;
        }
    }
    if (write_bootloader_message_ab(&boot_ab, misc_blk_device)==-1) {
        log_wrapper(LOG_LEVEL_ERROR, "Failed to set bootloader message\n");
        return -1;
    }
    if (get_bootloader_message_ab(&boot_ab, misc_blk_device)==-1) {
        log_wrapper(LOG_LEVEL_ERROR, "Failed to get_misc_blk_device\n");
        return -1;
    }
    // For each slot
    for (int i = 0; i < boot_ab.bootloader_ctrl.nb_slot; ++i) {
        slot_info[i] = &boot_ab.bootloader_ctrl.slot_info[i];
        if (slot_info[i]->priority >= max_priority) {
            max_index = i;
            max_priority = slot_info[i]->priority;
        }
        snprintf(log, sizeof(log), "<after> slot %d, priority: %d, successful_boot: %d, tries_remaining: %d, verity_corrupted: %d \n",
        i, slot_info[i]->priority,slot_info[i]->successful_boot,slot_info[i]->tries_remaining,slot_info[i]->verity_corrupted);
        log_wrapper(LOG_LEVEL_INFO, log);
    }
    snprintf(log, sizeof(log), "<after> max_index: %d, max_priority: %d \n\n",
    max_index, max_priority);
    log_wrapper(LOG_LEVEL_INFO, log);
    return 0;
}

int handle_responses(char *buf) {
    if (strncmp(buf, "start_ota", sizeof("start_ota")-1)==0) {
        return START_OTA;
    } else if (strncmp(buf, "package_ready", sizeof("package_ready")-1)==0) {
        return PACKAGE_READY;
    } else if (strncmp(buf, "package_not_ready", sizeof("package_not_ready")-1) == 0) {
        return PACKAGE_NOT_READY;
    } else if (strncmp(buf, "start_install", sizeof("start_install")-1) == 0) {
        return START_INSTALL;
    } else if (strncmp(buf, "start_rollback", sizeof("start_rollback")-1) == 0) {
        return ROLLBACK;
    } else if (strncmp(buf, "debug_slot_info", sizeof("debug_slot_info")-1) == 0) {
        return DEBUG_GET_SLOT_INFO;
    } else if (strncmp(buf, "mount", sizeof("mount")-1) == 0) {
        return DEBUG_MOUNT;
    } else if (strncmp(buf, "umount", sizeof("umount")-1) == 0) {
        return DEBUG_UMOUNT;
    } else {
        return UNDEFIINED;
    }
}

int main(int argc, char* argv[]) {
    char buf[256];
    PCI_TTY_Config config;
    enum Response response;
    char log[256];

    if (argc != 1 && argc != 3) {
        log_wrapper(LOG_LEVEL_INFO, "Usage:       ./ota_coordinator\n");
        log_wrapper(LOG_LEVEL_INFO, "             ./ota_coordinator <pci_read> <pci_write>\n");
        log_wrapper(LOG_LEVEL_INFO, "For example:./ota_coordinator\n");
        log_wrapper(LOG_LEVEL_INFO, "             ./ota_coordinator 0000:00:0f.0 0000:00:13.0\n");
        return EXIT_FAILURE;
    }

    config.pci_read = argc==3? argv[1]:"0000:00:0f.0";
    config.pci_write = argc==3? argv[2]:"0000:00:13.0";

    if (build_connection(&config)!=0) {
        log_wrapper(LOG_LEVEL_ERROR, "Failed to build connection.\n");
        return EXIT_FAILURE;
    }

    while(1) {
        memset(buf, 0, sizeof(buf));

        if (receive_message(config.fd_read, buf, sizeof(buf) - 1)==0) {
            //handle
            response = handle_responses(buf);
            snprintf(log, sizeof(log), "Received: response code=%d, buf=%s\n", response, buf);
            log_wrapper(LOG_LEVEL_INFO, log);
            switch (response) {
                case START_OTA:
                    handle_start_ota(&config);
                    break;
                case PACKAGE_READY:
                    handle_ota_package_ready(&config);
                    break;
                case PACKAGE_NOT_READY:
                    handle_ota_package_not_ready(&config);
                    break;
                case START_INSTALL:
                    handle_start_install(&config);
                    break;
                case ROLLBACK:
                    handle_rollback();
                    break;
                // there are for debug only
                case DEBUG_GET_SLOT_INFO:
                    debug_get_slot_info();
                    break;
                case DEBUG_MOUNT:
                    debug_mount();
                    break;
                case DEBUG_UMOUNT:
                    debug_umount();
                    break;
                case UNDEFIINED:
                    break;
            }
        }
    }

    close(config.fd_read);
    close(config.fd_write);
    return EXIT_SUCCESS;
}

