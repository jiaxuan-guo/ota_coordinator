#include "ota_coordinator.h"

#define SYSFS_PCI_DEVICES "/sys/bus/pci/devices"
#define BAUD_RATE B115200 //for both read and write

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

int find_tty_by_pci_addr() {
    DIR *dir;
    struct dirent *entry;
    int ret = -2;

    // Open the PCI devices directory
    dir = opendir(SYSFS_PCI_DEVICES);
    if (!dir) {
        log_wrapper(LOG_LEVEL_ERROR, "opendir");
        return ret;
    }

    // Iterate over each PCI device
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_LNK) {
            if (strcmp(entry->d_name, config.pci_read) == 0){
                find_tty_by_pci_addr_helper(entry, config.tty_read, &ret);
            } else if (strcmp(entry->d_name, config.pci_write) == 0){
                find_tty_by_pci_addr_helper(entry, config.tty_write, &ret);
            }
        }
    }

    closedir(dir);
    return ret;
}

int set_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        log_wrapper(LOG_LEVEL_ERROR, "fcntl");
        return -1;
    }
    flags &= ~O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1) {
        log_wrapper(LOG_LEVEL_ERROR, "fcntl");
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

int build_connection() {
    char log[256];
    // open both read and write tty devices
    if (find_tty_by_pci_addr() == 0) {
        snprintf(log, sizeof(log), "\n<config>\npci_read:%s  tty_read:%s \npci_write:%s  tty_write:%s\n",
        config.pci_read, config.tty_read, config.pci_write, config.tty_write);
        log_wrapper(LOG_LEVEL_INFO, log);
        config.fd_read = open(config.tty_read, O_RDWR | O_NOCTTY);
        if (config.fd_read == -1) {
            snprintf(log, sizeof(log), "open fd_read: %s\n", strerror(errno));
            log_wrapper(LOG_LEVEL_ERROR, log);
            return -1;
        }
        config.fd_write = open(config.tty_write, O_RDWR | O_NOCTTY);
        if (config.fd_write == -1) {
            snprintf(log, sizeof(log), "open fd_write: %s\n", strerror(errno));
            log_wrapper(LOG_LEVEL_ERROR, log);
            return -1;
        }
    } else {
        log_wrapper(LOG_LEVEL_ERROR, "TTY devices not found for the given PCI addr.\n");
        return -1;
    }

    // set uart configs
    config_uart(config.fd_write, 1);
    config_uart(config.fd_read, 0);

    return 0;
}

int send_message(int fd, const char *message) {
    char log[256];
    int n = write(fd, message, strlen(message));
    if (n < 0) {
        log_wrapper(LOG_LEVEL_ERROR, "Write failed - ");
        return -1;
    }
    tcdrain(fd);
    snprintf(log, sizeof(log), "Sent: %s\n", message);
    log_wrapper(LOG_LEVEL_INFO, log);
    return 0;
}

int receive_message(int fd, char *buf, size_t size) {
    if (read(fd, buf, size)==-1) {
        log_wrapper(LOG_LEVEL_ERROR, "read error");
        return -1;
    }
    return 0;
}