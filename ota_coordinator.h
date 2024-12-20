#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <poll.h>
#include <android/log.h>
#include <cutils/properties.h>
#include <cutils/android_reboot.h>
#include "bootloader_message.h"
#include <sys/mount.h>
#include <pthread.h>
#include <errno.h>

#define IS_RECOVERY (access("/system/bin/recovery", F_OK) == 0)
#define LOG_TAG "ota_coordinator"
#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__);
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__);
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__);
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__);
#define EVENT_SIZE  (sizeof(struct inotify_event))
#define EVENT_BUF_LEN     (1024 * (EVENT_SIZE + 16))

typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR
} LogLevel;
void log_wrapper(LogLevel level, const char *log);

typedef struct {
    const char *pci_read;
    const char *pci_write;
    char tty_read[256];
    char tty_write[256];
    int fd_read;
    int fd_write;
} PCI_TTY_Config;

PCI_TTY_Config config;

// If adding new responses here, please also update
// handle_responses() as well as handle logic in main()
enum RESPONSE{
    START_OTA=0,
    START_FACTORY_RESET,
    PACKAGE_READY,
    PACKAGE_NOT_READY,
    FACTORY_RESET,
    START_INSTALL,
    OTA_UPDATE_PROCESS,
    OTA_UPDATE_DONE,
    OTA_UPDATE_ABORT,
    FACTORY_RESET_PROCESS,
    DEBUG_ROLLBACK,
    DEBUG_GET_SLOT_INFO,
    DEBUG_MOUNT,
    DEBUG_UMOUNT,
    DEBUG_INOTIFY,
    UNDEFINED=255
};

enum OPERATION{
    OTA_UPDATE=0,
    OTA_FAKE_UPDATE,
    WIPE_DATA
};

typedef struct {
    char device[64];
    char mount_point[64];
    char fs_type[16];
    char options[64];
} FstabEntry;

// tty settings
void find_tty_by_pci_addr_helper(struct dirent *entry, char* tty, int *count);
int find_tty_by_pci_addr();
int set_blocking(int fd);
void config_uart(int fd, int isICANON);
int build_connection();
int send_message(int fd, const char *message);
int receive_message(int fd, char *buf, size_t size);

// misc partition
int read_misc_partition(void* buf, size_t size, char *misc_blk_device, size_t offset);
int write_misc_partition(const void* buf, size_t size, char *misc_blk_device, size_t offset);
int get_misc_blk_device(char *misc_blk_device);
int get_bootloader_message(bootloader_message *boot, char* misc_blk_device);
int get_bootloader_message_ab(bootloader_message_ab *boot_ab, char* misc_blk_device);
int write_bootloader_message(bootloader_message *boot, char* misc_blk_device);
int write_bootloader_message_ab(bootloader_message_ab *boot_ab, char* misc_blk_device);
int write_data_to_bcb(enum OPERATION operation);

// handle responses
int handle_responses(char *buf);
int handle_start_ota();
int handle_factory_reset();
int handle_ota_package_ready();
int is_boot_cmd_empty(bootloader_message* boot);
int isprint(int c);
int notify_and_shutdown();
int handle_ota_package_not_ready();
int handle_install_done();
int handle_start_factory_reset();
int do_shutdown();
int redirect_log ();
int handle_rollback();
int debug_get_slot_info();
int debug_mount();
int debug_umount();
int debug_inotify();

//server
void *factory_reset();
void *ota_update();

int recovery_log_fd;