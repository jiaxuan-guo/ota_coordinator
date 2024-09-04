#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <poll.h>


#define SYSFS_PCI_DEVICES "/sys/bus/pci/devices"
#define BAUD_RATE B115200 //for both read and write

typedef struct {
    const char *pci_read;
    const char *pci_write;
    char tty_read[256];
    char tty_write[256];
} PCI_TTY_Config;

int find_tty_by_pci_addr(PCI_TTY_Config *config) {
    DIR *dir;
    struct dirent *entry;
    struct dirent *tty_entry;
    DIR *tty_dir;
    char path[256];
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
                snprintf(path, sizeof(path), "%s/%s/tty", SYSFS_PCI_DEVICES, entry->d_name);
                tty_dir = opendir(path);
                if (tty_dir) {
                    while ((tty_entry = readdir(tty_dir)) != NULL) {
                        if (strncmp(tty_entry->d_name, "tty", 3) == 0) {
                            snprintf(config->tty_read, sizeof(config->tty_read), "/dev/%s", tty_entry->d_name);
                            ++ret;
                            closedir(tty_dir);
                            break;
                        }
                    }
                }
            } else if (strcmp(entry->d_name, config->pci_write) == 0){
                snprintf(path, sizeof(path), "%s/%s/tty", SYSFS_PCI_DEVICES, entry->d_name);
                tty_dir = opendir(path);
                if (tty_dir) {
                    while ((tty_entry = readdir(tty_dir)) != NULL) {
                        if (strncmp(tty_entry->d_name, "tty", 3) == 0) {
                            snprintf(config->tty_write, sizeof(config->tty_write), "/dev/%s", tty_entry->d_name);
                            ++ret;
                            closedir(tty_dir);
                            break;
                        }
                    }
                }
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

int main() {
    int fd_read;
    int fd_write;

    PCI_TTY_Config config = {
        .pci_read  = "0000:00:0f.0",
        .pci_write = "0000:00:13.0",
        .tty_read = "",
        .tty_write = "",
    };

    if (find_tty_by_pci_addr(&config) == 0) {
        printf("<config>\npci_read:%s, tty_read:%s,\npci_write:%s, tty_write:%s\n",
        config.pci_read, config.tty_read, config.pci_write, config.tty_write);
        fd_read = open(config.tty_read, O_RDWR | O_NOCTTY);
        if (fd_read == -1) {
            perror("open");
            return EXIT_FAILURE;
        }
        fd_write = open(config.tty_write, O_RDWR | O_NOCTTY);
        if (fd_write == -1) {
            perror("open");
            return EXIT_FAILURE;
        }
    } else {
        perror("TTY devices not found for the given PCI addr.\n");
        return EXIT_FAILURE;
    }

    config_uart(fd_write, 1);
    config_uart(fd_read, 0);

    const char *messages[] = {"[VM] OTA Available\r\n", "[VM] bbb\r\n", "[VM] ccc\r\n"};
    for(int i = 0; i < 3; i++) {
        int n = write(fd_write, messages[i], strlen(messages[i]));
        if (n < 0) {
            perror("Write failed - ");
            return EXIT_FAILURE;
        }
        tcdrain(fd_write);
    }

    struct pollfd fds[1];
    fds[0].fd = fd_read;
    fds[0].events = POLLIN;

   while(1) {
        char buf[256];
        memset(buf, 0, sizeof(buf));

        int poll_result = poll(fds, 1, -1); // Wait indefinitely for data

        if (poll_result > 0) {
            if (fds[0].revents & POLLIN) {
                int n = read(fd_read, buf, sizeof(buf) - 1);

                if (n < 0) {
                    perror("Read failed -");
                    return -1;
                } else if (n > 0) {
                    printf("Received n(%d):  %s\n", n,buf);
                }
            }
        }
    }




    close(fd_read);
    close(fd_write);
    return EXIT_SUCCESS;
}
