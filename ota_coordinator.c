
#include "ota_coordinator.h"

#define ANDROID_RB_PROPERTY "sys.powerctl"
#define OTA_PACKAGE "/data/ota/xxx.zip"
#define RECOVERY_CMD "boot-recovery"
// #define RECOVERY_PATH "recovery\n--wipe_data"
#define RECOVERY_PATH "recovery\n--update_package=/udiska/aaos_iasw-ota-eng.jade.zip"

int is_recovery;
void log_wrapper(LogLevel level, const char *log) {
    if (is_recovery) {
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
        return 0;

    for (size_t i = 0; i < sizeof(boot->command); ++i) {
        if (boot->command[i] == '\0')
            return 1;
        if (!isprint(boot->command[i]))
            break;
    }

    memset(boot->command, 0, sizeof(boot->command));
    return 0;
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
    if (!is_boot_cmd_empty(&boot)) {
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

int handle_rollback() {
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
    } else {
        return UNDEFIINED;
    }
}

int main(int argc, char* argv[]) {
    char buf[256];
    PCI_TTY_Config config;
    enum Response response;
    char log[256];
    is_recovery = access("/system/bin/recovery", F_OK) == 0;

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
                case UNDEFIINED:
                    break;
            }
        }
    }

    close(config.fd_read);
    close(config.fd_write);
    return EXIT_SUCCESS;
}

