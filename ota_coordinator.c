
#include "ota_coordinator.h"

#define BOOTLOADER_MESSAGE_OFFSET_IN_MISC 0
#define OTA_PACKAGE "/data/ota/xxx.zip"

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

int get_bootloader_message(bootloader_message *boot, char* misc_blk_device) {
    return read_misc_partition(boot, sizeof(*boot), misc_blk_device, BOOTLOADER_MESSAGE_OFFSET_IN_MISC);
}

int write_recovery_to_bcb() {
    char misc_blk_device[256];
    bootloader_message boot;

    if (get_misc_blk_device(misc_blk_device)==-1) {
        ALOGE("Failed to get_misc_blk_device\n");
        return -1;
    }
    ALOGI("misc_blk_device: %s\n", misc_blk_device);

    if (get_bootloader_message(&boot, misc_blk_device)==-1) {
        ALOGE("Failed to get_misc_blk_device\n");
        return -1;
    }
    ALOGI("<get_bootloader_message>\nboot.command: %s, boot.status: %s, boot.recovery: %s, boot.stage: %s\n\n",
    boot.command, boot.status, boot.recovery, boot.stage);

    // if (!wait_for_device(misc_blk_device, err)) {
    //     return false;
    // }

    // Update the boot command field if it's empty, and preserve
    // the other arguments in the bootloader message.
    if (!is_boot_cmd_empty(&boot)) {
        strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
        if (write_misc_partition(&boot, sizeof(boot), misc_blk_device, BOOTLOADER_MESSAGE_OFFSET_IN_MISC)==-1) {
            ALOGE("Failed to set bootloader message\n");
            return -1;
        }
        ALOGI("<write_misc_partition>\nboot.command: %s, boot.status: %s, boot.recovery: %s, boot.stage: %s\n\n",
        boot.command, boot.status, boot.recovery, boot.stage);
    } else {
        ALOGI("boot.command is not empty, don't write recovery into it.\n");
    }
    return 0;
}

int handle_start_ota(PCI_TTY_Config *config) {
    if (send_message(config->fd_write, "need_to_ota\n")) {
        ALOGE("send_message failed!\n");
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
        ALOGI("File %s deleted successfully.\n", OTA_PACKAGE);
    } else {
        ALOGE("remove");
        return -1;
    }

    return 0;
}

//All VMs set the next boot target as recovery, notify SOS, shutdown
int handle_ota_package_ready(PCI_TTY_Config *config) {
    //write recovery into BCB(bootloader control block)
    write_recovery_to_bcb();

    //notify SOS
    if (send_message(config->fd_write, "vm_shutdown\n")) {
        ALOGE("send_message failed!\n");
        return EXIT_FAILURE;
    }

    // shutdown
    int result = system("reboot -p");
    if (result == -1) {
        ALOGE("system");
        return 1;
    } else {
        ALOGI("Script executed with exit status: %d\n", WEXITSTATUS(result));
    }
    return 0;
}

// install the package and send the notification to SOS
int handle_start_install(PCI_TTY_Config *config) {
    // todo: start to install the package


    // installed well
    if (send_message(config->fd_write, "successful\n")) {
        ALOGE("send_message failed!\n");
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

    if (argc != 1 && argc != 3) {
        ALOGI("Usage:       %s\n", argv[0]);
        ALOGI("             %s <pci_read> <pci_write>\n", argv[0]);
        ALOGI("For example: %s\n", argv[0]);
        ALOGI("             %s 0000:00:0f.0 0000:00:13.0\n", argv[0]);
        return EXIT_FAILURE;
    }

    config.pci_read = argc==3? argv[1]:"0000:00:0f.0";
    config.pci_write = argc==3? argv[2]:"0000:00:13.0";

    if (build_connection(&config)!=0) {
        ALOGE("Failed to build connection.\n");
        return EXIT_FAILURE;
    }

    while(1) {
        memset(buf, 0, sizeof(buf));

        if (receive_message(config.fd_read, buf, sizeof(buf) - 1)==0) {
            //handle
            response = handle_responses(buf);
            ALOGI("Received: code=%d, buf=%s\n", response, buf);
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

