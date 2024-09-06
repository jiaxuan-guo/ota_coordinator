
#include "ota_coordinator.h"

#define SYSFS_PCI_DEVICES "/sys/bus/pci/devices"
#define BAUD_RATE B115200 //for both read and write
#define BOOTLOADER_MESSAGE_OFFSET_IN_MISC 0

int send_message(int fd, const char *message) {
    int n = write(fd, message, strlen(message));
    if (n < 0) {
        perror("Write failed - ");
        return -1;
    }
    // tcdrain(fd);
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

void config_uart(int fd, int isICANON) {
    struct termios options;

    tcgetattr(fd, &options);
    cfsetispeed(&options, BAUD_RATE);
    cfsetospeed(&options, BAUD_RATE);

    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_lflag &= ~(ECHO | ECHOE | ISIG);
    if (!isICANON) {
        options.c_lflag &= ~ICANON;
    }
    options.c_oflag &= ~OPOST;

    tcsetattr(fd, TCSANOW, &options);
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

//All VMs set the next boot target as recovery, notify SOS, shutdown
int handle_ota_package_ready(PCI_TTY_Config *config) {
    //write recovery into BCB(bootloader control block)
    write_recovery_to_bcb();

    //notify SOS
    if (send_message(config->fd_write, "vm_shutdown")) {
        perror("send_message failed!\n");
        return EXIT_FAILURE;
    }

    // shutdown
    int result = system("reboot -p");
    if (result == -1) {
        perror("system");
        return 1;
    } else {
        printf("Script executed with exit status: %d\n", WEXITSTATUS(result));
    }
    return 0;
}

int main(int argc, char* argv[]) {
    // struct pollfd fds[1];
    // int poll_result;
    // char buf[256];
    PCI_TTY_Config config;

    if (argc != 3) {
        printf("<Usage>: %s <pci_read> <pci_write>\n", argv[0]);
        printf("For example: ./ota_coordinator 0000:00:0f.0 0000:00:13.0\n");
        return EXIT_FAILURE;
    }

    config.pci_read  = argv[1];
    config.pci_write  = argv[2];
#if 0
    // open both read and write tty devices
    if (find_tty_by_pci_addr(&config) == 0) {
        printf("<config>\npci_read:%s  tty_read:%s \npci_write:%s  tty_write:%s\n",
        config.pci_read, config.tty_read, config.pci_write, config.tty_write);

        config.fd_read = open(config.tty_read, O_RDWR | O_NOCTTY);
        if (config.fd_read == -1) {
            perror("open fd_read");
            return EXIT_FAILURE;
        }
        config.fd_write = open(config.tty_write, O_RDWR | O_NOCTTY);
        if (config.fd_write == -1) {
            perror("open fd_write");
            return EXIT_FAILURE;
        }
    } else {
        perror("TTY devices not found for the given PCI addr.\n");
        return EXIT_FAILURE;
    }

    // set uart configs
    config_uart(config.fd_write, 1);
    config_uart(config.fd_read, 0);

    // init message
    if (send_message(config.fd_write, "hello\r\n")) {
        perror("send_message failed!\n");
        return EXIT_FAILURE;
    }
    tcdrain(config.fd_write);
    if (send_message(config.fd_write, "need_to_ota\r\n")) {
        perror("send_message failed!\n");
        return EXIT_FAILURE;
    }
    fds[0].fd = config.fd_read;
    fds[0].events = POLLIN;
    tcdrain(config.fd_write);
#endif
    handle_ota_package_ready(&config);
//    while(1) {
//         memset(buf, 0, sizeof(buf));
//         poll_result = poll(fds, 1, -1); // Wait indefinitely for data

//         if (poll_result > 0) {
//             if (fds[0].revents & POLLIN) {
//                 int n = read(config.fd_read, buf, sizeof(buf) - 1);

//                 if (n < 0) {
//                     perror("Read failed -");
//                     return EXIT_FAILURE;
//                 } else if (n > 0) {
//                     //handle
//                     if (strncmp(buf, "package_ready", sizeof("package_ready")-1) == 0) {
//                         printf("package ready\n");
//                         handle_ota_package_ready(&config);
//                     } else if (strncmp(buf, "package_not_ready", sizeof("package_not_ready")-1) == 0) {
//                         printf("package_not_ready\n");
//                     }
//                 }
//             }
//         }
//     }

    close(config.fd_read);
    close(config.fd_write);
    return EXIT_SUCCESS;
}
