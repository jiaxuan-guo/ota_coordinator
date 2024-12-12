#include "ota_coordinator.h"

#include <sys/inotify.h>
#define ANDROID_RB_PROPERTY "sys.powerctl"
#define ANDROID_OTA_LAST_SLOT_PROPERTY "persist.vendor.ota_coordinator.last_slot_suffix"
#define ANDROID_OTA_FAKE_UPDATE_PROPERTY "persist.vendor.ota_coordinator.fake_update"
#define ANDROID_BOOT_SLOT_PROPERTY "ro.boot.slot_suffix"

#define OTA_PACKAGE_PATH "/data/vendor/ota"
#define OTA_PACKAGE_FILENAME "aaos_iasw-ota.zip"
#define OTA_PACKAGE OTA_PACKAGE_PATH "/" OTA_PACKAGE_FILENAME

// there is no logcat under recovery, redirect log to /tmp/ota_coordinator.log
int redirect_log () {
    recovery_log_fd = open("/tmp/ota_coordinator.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    return 0;
}

void log_wrapper(LogLevel level, const char *log) {
    if (IS_RECOVERY) {
        write(recovery_log_fd, log, strlen(log));
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

int handle_start_ota() {
    if (send_message(config.fd_write, "need_to_ota\n")) {
        log_wrapper(LOG_LEVEL_ERROR, "send need_to_ota failed!\n");
        return -1;
    }
    return 0;
}

int handle_start_factory_reset() {
        if (send_message(config.fd_write, "need_to_factory_reset\n")) {
            log_wrapper(LOG_LEVEL_ERROR, "send need_to_factory_reset failed!\n");
            return -1;
        }
    return 0;
}

// remove package and drop this update
int handle_ota_package_not_ready(){
    if (remove(OTA_PACKAGE) == 0) {
        log_wrapper(LOG_LEVEL_INFO, "OTA_PACKAGE deleted successfully.\n");
    } else {
        log_wrapper(LOG_LEVEL_ERROR, "remove");
        return -1;
    }
    return 0;
}

int handle_factory_reset() {
    write_data_to_bcb(WIPE_DATA);
    return notify_and_shutdown(config);
}

int do_shutdown() {
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

// Notify SOS and shutdown
int notify_and_shutdown() {
    usleep(3000000);
    if (send_message(config.fd_write, "vm_shutdown\n")) {
        log_wrapper(LOG_LEVEL_ERROR, "send vm_shutdown failed!\n");
        return -1;
    }
    return do_shutdown();
}

// All VMs set the next boot target as recovery, notify SOS, shutdown
int handle_ota_package_ready() {
    FILE *file = fopen(OTA_PACKAGE, "r");
    int ret;

    if (file) {
        // If Android OTA package exist, do OTA update
        fclose(file);
        write_data_to_bcb(OTA_UPDATE);
    } else {
        // else, return success directly.
        ret = property_set(ANDROID_OTA_FAKE_UPDATE_PROPERTY, "1");
        write_data_to_bcb(OTA_FAKE_UPDATE);
    }
    return notify_and_shutdown();
}

// Send the notification to SOS
int handle_install_done() {
    // installed well
    if (send_message(config.fd_write, "successful\n")) {
        log_wrapper(LOG_LEVEL_ERROR, "send successful failed!\n");
        return -1;
    }
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
        log_wrapper(LOG_LEVEL_ERROR, "Failed to get_bootloader_message_ab\n");
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

int mount_on(char *source, char* target, char *fstype) {
    char log[256];

    if (mount(source, target, fstype, 0, NULL) == -1) {

        snprintf(log, sizeof(log), "Error mounting %s on %s: %s\n", source, target, strerror(errno));
        log_wrapper(LOG_LEVEL_ERROR, log);
        return 1;
    }

    snprintf(log, sizeof(log), "Successfully mounted %s on %s\n", source, target);
    log_wrapper(LOG_LEVEL_INFO, log);
    return 0;
}

int debug_mount() {
    if (mount("myfs", "/data/vendor/ota", "virtiofs", 0, NULL) == -1) {
        log_wrapper(LOG_LEVEL_ERROR,"Error mounting myfs on /data/vendor/ota\n");
        return 1;
    }

    log_wrapper(LOG_LEVEL_INFO,"Successfully mounted myfs on /data/vendor/ota\n");
    return 0;
}

int debug_umount() {
    if (umount("/data/vendor/ota") == -1) {
        log_wrapper(LOG_LEVEL_ERROR,"Error umounting myfs on /data/vendor/ota\n");
        return 1;
    }

    log_wrapper(LOG_LEVEL_INFO,"Successfully umounted myfs on /data/vendor/ota\n");
    return 0;
}

int get_ota_status(int *status) {
    char line[256];
    FILE *file = fopen("/tmp/ota_status", "r");
    char log[256];

    log_wrapper(LOG_LEVEL_INFO, "get_ota_status start\n");
    if (file == NULL) {
        snprintf(log, sizeof(log), "Failed to open /tmp/ota_status: %s\n", strerror(errno));
        log_wrapper(LOG_LEVEL_ERROR, log);
        return -1;
    }

    while (fgets(line, sizeof(line), file)) {
        char *equalsSign = strchr(line, '=');
        if (equalsSign != NULL) {
            char *value = equalsSign + 1;

            size_t len = strlen(value);
            if (len > 0 && value[len - 1] == '\n') {
                value[len - 1] = '\0';
            }
            *status = atoi(value);
            snprintf(log, sizeof(log), "value: %s, status: %d\n", value, *status);
            log_wrapper(LOG_LEVEL_INFO, log);
        }
    }
    fclose(file);
    return 0;
}

int debug_inotify() {
    int length, i=0;
    int fd;
    int wd;
    int flags;
    char buffer[EVENT_BUF_LEN];
    char log[64];
    int keep=1;
    int status = -1;

    log_wrapper(LOG_LEVEL_ERROR, "debug_inotify start\n");
    fd = inotify_init();
    if (fd < 0) {
        log_wrapper(LOG_LEVEL_ERROR, "inotify_init failed");
        return -1;
    }
    flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    wd = inotify_add_watch(fd, "/tmp", IN_MODIFY | IN_CREATE | IN_DELETE);
    if (wd == -1) {
        log_wrapper(LOG_LEVEL_ERROR, "inotify_add_watch failed");
        close(fd);
        return -1;
    }
    log_wrapper(LOG_LEVEL_INFO, "inotify_add_watch done\n");

    while (keep) {
        i = 0;
        length = read(fd, buffer, EVENT_BUF_LEN);
        if (length < 0) {
            if (errno == EAGAIN) {
                usleep(100000);
                continue;
            } else {
                log_wrapper(LOG_LEVEL_ERROR, "read");
                break;
            }
        }

        while (i < length) {
            struct inotify_event *event = (struct inotify_event *) &buffer[i];
            if (event->len && strncmp(event->name, "ota_status", sizeof("ota_status")-1)==0) {
                if (event->mask & IN_CREATE) {
                    get_ota_status(&status);
                    snprintf(log, sizeof(log), "The file %s was created, status: %d\n", event->name, status);
                    log_wrapper(LOG_LEVEL_INFO, log);
                    if(status==0) {
                        keep=0;
                        if (send_message(config.fd_write, "vm_status_successful\n")) {
                            log_wrapper(LOG_LEVEL_ERROR, "send vm_status_successful failed!\n");
                            return -1;
                        }
                        // notify_and_shutdown();
                        break;
                    }
                } else if (event->mask & IN_DELETE) {
                    snprintf(log, sizeof(log), "The file %s was deleted.\n", event->name);
                    log_wrapper(LOG_LEVEL_INFO, log);
                    keep=0;
                } else if (event->mask & IN_MODIFY) {
                    get_ota_status(&status);
                    snprintf(log, sizeof(log), "The file %s was modified, status: %d\n", event->name, status);
                    log_wrapper(LOG_LEVEL_INFO, log);
                    if(status==0) {
                        keep=0;
                        if (send_message(config.fd_write, "vm_status_successful\n")) {
                            log_wrapper(LOG_LEVEL_ERROR, "send vm_status_successful failed!\n");
                            return -1;
                        }
                        // notify_and_shutdown();
                        break;
                    }
                }
            }
            i += EVENT_SIZE + event->len;
        }
    }

    inotify_rm_watch(fd, wd);
    log_wrapper(LOG_LEVEL_INFO, "inotify_rm_watch done\n");
    close(fd);
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
        log_wrapper(LOG_LEVEL_ERROR, "Failed to get_bootloader_message_ab\n");
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
        log_wrapper(LOG_LEVEL_ERROR, "Failed to get_bootloader_message_ab\n");
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

int handle_factory_reset_process() {
    return notify_and_shutdown();
}

int handle_ota_update_process() {
    return notify_and_shutdown();
}

int handle_responses(char *buf) {
    if (strncmp(buf, "start_ota", sizeof("start_ota")-1)==0) {
        return START_OTA;
    } else if (strncmp(buf, "start_factory_reset", sizeof("start_factory_reset")-1)==0) {
        return START_FACTORY_RESET;
    } else if (strncmp(buf, "package_ready", sizeof("package_ready")-1)==0) {
        return PACKAGE_READY;
    } else if (strncmp(buf, "package_not_ready", sizeof("package_not_ready")-1) == 0) {
        return PACKAGE_NOT_READY;
    } else if (strncmp(buf, "start_to_factory_reset", sizeof("start_to_factory_reset")-1) == 0) {
        return FACTORY_RESET;
    } else if (strncmp(buf, "start_install", sizeof("start_install")-1) == 0) {
        return START_INSTALL;
    } else if (strncmp(buf, "ota_process", sizeof("ota_process")-1) == 0) {
        return OTA_UPDATE_PROCESS;
    } else if (strncmp(buf, "ota_done", sizeof("ota_done")-1) == 0) {
        return OTA_UPDATE_DONE;
    } else if (strncmp(buf, "factory_reset_process", sizeof("factory_reset_process")-1) == 0) {
        return FACTORY_RESET_PROCESS;
    } else if (strncmp(buf, "ota_abort", sizeof("ota_abort")-1) == 0) {
        return OTA_UPDATE_ABORT;
    // for debug purpose
    } else if (strncmp(buf, "start_rollback", sizeof("start_rollback")-1) == 0) {
        return DEBUG_ROLLBACK;
    } else if (strncmp(buf, "debug_slot_info", sizeof("debug_slot_info")-1) == 0) {
        return DEBUG_GET_SLOT_INFO;
    } else if (strncmp(buf, "mount", sizeof("mount")-1) == 0) {
        return DEBUG_MOUNT;
    } else if (strncmp(buf, "umount", sizeof("umount")-1) == 0) {
        return DEBUG_UMOUNT;
    } else if (strncmp(buf, "inotify", sizeof("inotify")-1) == 0) {
        return DEBUG_INOTIFY;
    } else {
        return UNDEFINED;
    }
}

void *ota_update() {
    enum RESPONSE response;
    char buf[256];
    char log[256];
    char is_fake_update[PROPERTY_VALUE_MAX];
    char last_slot[PROPERTY_VALUE_MAX];
    char curr_slot[PROPERTY_VALUE_MAX];
    int ret;

    if (build_connection()!=0) {
        log_wrapper(LOG_LEVEL_ERROR, "Failed to build connection.\n");
        return NULL;
    }

    if (!IS_RECOVERY) {
        ret = property_get(ANDROID_OTA_FAKE_UPDATE_PROPERTY, is_fake_update, "0");
        snprintf(log, sizeof(log), "%s is %s, ret value: %d", ANDROID_OTA_FAKE_UPDATE_PROPERTY, is_fake_update, ret);
        log_wrapper(LOG_LEVEL_DEBUG, log);
        if (strncmp(is_fake_update, "1", 1) == 0) {
            ret = property_set(ANDROID_OTA_FAKE_UPDATE_PROPERTY, "0");
            handle_install_done();
            log_wrapper(LOG_LEVEL_DEBUG, "handle_install_done done");
        }

        ret = property_get(ANDROID_OTA_LAST_SLOT_PROPERTY, last_slot, "0");
        snprintf(log, sizeof(log), "%s is %s, ret value: %d", ANDROID_OTA_LAST_SLOT_PROPERTY, last_slot, ret);
        log_wrapper(LOG_LEVEL_DEBUG, log);

        ret = property_get(ANDROID_BOOT_SLOT_PROPERTY, curr_slot, "0");
        snprintf(log, sizeof(log), "%s is %s, ret value: %d", ANDROID_BOOT_SLOT_PROPERTY, curr_slot, ret);
        log_wrapper(LOG_LEVEL_DEBUG, log);

        if (strncmp(last_slot, curr_slot, 2)) {
            ret = property_set(ANDROID_OTA_LAST_SLOT_PROPERTY, curr_slot);
            snprintf(log, sizeof(log), "Set %s as %s, ret: %d", ANDROID_OTA_LAST_SLOT_PROPERTY, curr_slot, ret);
            log_wrapper(LOG_LEVEL_DEBUG, log);
            handle_install_done();
            log_wrapper(LOG_LEVEL_DEBUG, "handle_install_done done");
        }
    }

    if (IS_RECOVERY) {
        debug_inotify();
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
                    handle_start_ota();
                    break;
                case START_FACTORY_RESET:
                    handle_start_factory_reset();
                    break;
                case PACKAGE_READY:
                    handle_ota_package_ready();
                    break;
                case PACKAGE_NOT_READY:
                    handle_ota_package_not_ready();
                    break;
                case FACTORY_RESET:
                    handle_factory_reset();
                    break;
                case START_INSTALL:
                    handle_install_done();
                    break;
                case OTA_UPDATE_PROCESS:
                    handle_ota_update_process();
                    break;
                case OTA_UPDATE_DONE:
                    log_wrapper(LOG_LEVEL_INFO, "OTA update done!\n");
                    break;
                case OTA_UPDATE_ABORT:
                    handle_rollback();
                    break;
                case FACTORY_RESET_PROCESS:
                    handle_factory_reset_process();
                    break;
                // there are for debug only
                case DEBUG_ROLLBACK:
                    handle_rollback();
                    break;
                case DEBUG_GET_SLOT_INFO:
                    debug_get_slot_info();
                    break;
                case DEBUG_MOUNT:
                    debug_mount();
                    break;
                case DEBUG_UMOUNT:
                    debug_umount();
                    break;
                case DEBUG_INOTIFY:
                    debug_inotify();
                    break;
                case UNDEFINED:
                    break;
            }
        }
    }

    close(config.fd_read);
    close(config.fd_write);
    return NULL;
}
