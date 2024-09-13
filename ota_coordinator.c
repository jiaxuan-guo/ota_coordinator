
#include "ota_coordinator.h"

#define SYSFS_PCI_DEVICES "/sys/bus/pci/devices"
#define BAUD_RATE B115200 //for both read and write
#define BOOTLOADER_MESSAGE_OFFSET_IN_MISC 0
#define OTA_PACKAGE "/data/ota/xxx.zip"

int send_message(int fd, const char *message) {
    int n = write(fd, message, strlen(message));
    if (n < 0) {
        perror("Write failed - ");
        return -1;
    }
    tcdrain(fd);
    printf("Sent: %s\n", message);
    return 0;
}

int receive_message(int fd, char *buf, size_t size) {
    if (read(fd, buf, size)==-1) {
        perror("read error");
        return -1;
    }
    return 0;
}

void find_tty_by_pci_addr_helper(struct dirent *entry, char* tty, int *count) {
    char path[256];
    DIR *tty_dir;
    struct dirent *tty_entry;

    snprintf(path, sizeof(path), "%s/%s/tty", SYSFS_PCI_DEVICES, entry->d_name);
    tty_dir = opendir(path);
    if (tty_dir) {
        while ((tty_entry = readdir(tty_dir)) != NULL) {
            if (strncmp(tty_entry->d_name, "tty", 3) == 0) {
                snprintf(tty, 4+sizeof(tty_entry->d_name), "/dev/%s", tty_entry->d_name);
                closedir(tty_dir);
                ++(*count);
                return;
            }
        }
    }
}

int find_tty_by_pci_addr(PCI_TTY_Config *config) {
    DIR *dir;
    struct dirent *entry;
    int ret = -2;

    // Open the PCI devices directory
    dir = opendir(SYSFS_PCI_DEVICES);
    if (!dir) {
        perror("opendir");
        return ret;
    }

    // Iterate over each PCI device
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_LNK) {
            if (strcmp(entry->d_name, config->pci_read) == 0){
                find_tty_by_pci_addr_helper(entry, config->tty_read, &ret);
            } else if (strcmp(entry->d_name, config->pci_write) == 0){
                find_tty_by_pci_addr_helper(entry, config->tty_write, &ret);
            }
        }
    }

    closedir(dir);
    return ret;
}

int set_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl");
        return -1;
    }
    flags &= ~O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1) {
        perror("fcntl");
        return -1;
    }
    return 0;
}

void config_uart(int fd, int isICANON) {
    struct termios options;

    tcgetattr(fd, &options);
    cfsetispeed(&options, BAUD_RATE);
    cfsetospeed(&options, BAUD_RATE);

    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CRTSCTS;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_lflag &= ~(ECHO | ECHOE | ISIG);
    if (!isICANON) {
        options.c_lflag &= ~ICANON;
    }
    options.c_oflag &= ~OPOST;

    tcsetattr(fd, TCSANOW, &options);
    set_blocking(fd);
}

int isprint(int c) {
	return (unsigned)c-0x20 < 0x5f;
}

int CommandIsPresent(bootloader_message* boot) {
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
        perror("Failed to get_misc_blk_device\n");
        return -1;
    }
    printf("misc_blk_device: %s\n", misc_blk_device);

    if (get_bootloader_message(&boot, misc_blk_device)==-1) {
        perror("Failed to get_misc_blk_device\n");
        return -1;
    }
    printf("<get_bootloader_message>\nboot.command: %s, boot.status: %s, boot.recovery: %s, boot.stage: %s\n",
    boot.command, boot.status, boot.recovery, boot.stage);

    // if (!wait_for_device(misc_blk_device, err)) {
    //     return false;
    // }

    // Update the boot command field if it's empty, and preserve
    // the other arguments in the bootloader message.
    if (!CommandIsPresent(&boot)) {
        strlcpy(boot.command, "boot-recovery", sizeof(boot.command));
        if (write_misc_partition(&boot, sizeof(boot), misc_blk_device, BOOTLOADER_MESSAGE_OFFSET_IN_MISC)==-1) {
            perror("Failed to set bootloader message\n");
            return -1;
        }
    }
    return 0;
}

int handle_start_ota(PCI_TTY_Config *config) {
    if (send_message(config->fd_write, "need_to_ota\n")) {
        perror("send_message failed!\n");
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
        printf("File %s deleted successfully.\n", OTA_PACKAGE);
    } else {
        perror("remove");
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
        perror("send_message failed!\n");
        return EXIT_FAILURE;
    }

    // shutdown
    // int result = system("reboot -p");
    // if (result == -1) {
    //     perror("system");
    //     return 1;
    // } else {
    //     printf("Script executed with exit status: %d\n", WEXITSTATUS(result));
    // }
    return 0;
}

// install the package and send the notification to SOS
int handle_start_install(PCI_TTY_Config *config) {
    // todo: start to install the package


    // installed well
    if (send_message(config->fd_write, "installed_well\n")) {
        perror("send_message failed!\n");
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

int build_connection(PCI_TTY_Config *config) {
    // open both read and write tty devices
    if (find_tty_by_pci_addr(config) == 0) {
        printf("\n<config>\npci_read:%s  tty_read:%s \npci_write:%s  tty_write:%s\n",
        config->pci_read, config->tty_read, config->pci_write, config->tty_write);

        config->fd_read = open(config->tty_read, O_RDWR | O_NOCTTY);
        if (config->fd_read == -1) {
            perror("open fd_read");
            return -1;
        }
        config->fd_write = open(config->tty_write, O_RDWR | O_NOCTTY);
        if (config->fd_write == -1) {
            perror("open fd_write");
            return -1;
        }
    } else {
        perror("TTY devices not found for the given PCI addr.\n");
        return -1;
    }

    // set uart configs
    config_uart(config->fd_write, 1);
    config_uart(config->fd_read, 0);

    return 0;
}

int main(int argc, char* argv[]) {
    char buf[256];
    PCI_TTY_Config config;
    enum Response response;

    if (argc != 1 && argc != 3) {
        printf("Usage:       %s\n", argv[0]);
        printf("             %s <pci_read> <pci_write>\n", argv[0]);
        printf("For example: %s\n", argv[0]);
        printf("             %s 0000:00:0f.0 0000:00:13.0\n", argv[0]);
        return EXIT_FAILURE;
    }

    config.pci_read = argc==3? argv[1]:"0000:00:0f.0";
    config.pci_write = argc==3? argv[2]:"0000:00:13.0";

    if (build_connection(&config)!=0) {
        perror("Failed to build connection.\n");
        return EXIT_FAILURE;
    }

    while(1) {
        memset(buf, 0, sizeof(buf));

        if (receive_message(config.fd_read, buf, sizeof(buf) - 1)==0) {
            //handle
            response = handle_responses(buf);
            printf("Received: code=%d, buf=%s\n", response, buf);
            switch (response) {
                case START_OTA:
                // init message
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

