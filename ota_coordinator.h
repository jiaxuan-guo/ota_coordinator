#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <poll.h>

typedef struct {
    const char *pci_read;
    const char *pci_write;
    char tty_read[256];
    char tty_write[256];
    int fd_read;
    int fd_write;
} PCI_TTY_Config;

enum Response{
    START_OTA=0,
    PACKAGE_READY,
    PACKAGE_NOT_READY,
    UNDEFIINED
};

typedef struct {
    char device[64];
    char mount_point[64];
    char fs_type[16];
    char options[64];
} FstabEntry;

typedef struct{
    char command[32];
    char status[32];
    char recovery[768];
    // The 'recovery' field used to be 1024 bytes.  It has only ever
    // been used to store the recovery command line, so 768 bytes
    // should be plenty.  We carve off the last 256 bytes to store the
    // stage string (for multistage packages) and possible future
    // expansion.
    char stage[32];
    char reserved[1184];
    // The 'reserved' field used to be 224 bytes when it was initially
    // carved off from the 1024-byte recovery field. Bump it up to
    // 1184-byte so that the entire bootloader_message struct rounds up
    // to 2048-byte.
} bootloader_message;

// tty settings
int send_message(int fd, const char *message);
int receive_message(int fd, char *buf, size_t size);
void find_tty_by_pci_addr_helper(struct dirent *entry, char* tty, int *count);
int find_tty_by_pci_addr(PCI_TTY_Config *config);
int set_blocking(int fd);
void config_uart(int fd, int isICANON);

// misc partition
int write_recovery_to_bcb();
int get_misc_blk_device(char *misc_blk_device);
int get_bootloader_message(bootloader_message *boot, char* misc_blk_device);
int read_misc_partition(void* buf, size_t size, char *misc_blk_device, size_t offset);
int write_misc_partition(const void* buf, size_t size, char *misc_blk_device, size_t offset);
int CommandIsPresent(bootloader_message* boot);
int isprint(int c);

// handle responses
int handle_responses(char *buf);
int handle_start_ota(PCI_TTY_Config *config);
int handle_ota_package_ready(PCI_TTY_Config *config);
int handle_ota_package_not_ready(PCI_TTY_Config *config);