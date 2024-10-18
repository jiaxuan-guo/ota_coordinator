#include "ota_coordinator.h"

int main(int argc, char* argv[]) {
    pthread_t socket_thread, uart_thread;

    if (argc != 1 && argc != 3) {
        log_wrapper(LOG_LEVEL_INFO, "Usage:       ./ota_coordinator\n");
        log_wrapper(LOG_LEVEL_INFO, "             ./ota_coordinator <pci_read> <pci_write>\n");
        log_wrapper(LOG_LEVEL_INFO, "For example:./ota_coordinator\n");
        log_wrapper(LOG_LEVEL_INFO, "             ./ota_coordinator 0000:00:0f.0 0000:00:13.0\n");
        return EXIT_FAILURE;
    }

    config.pci_read = argc==3? argv[1]:"0000:00:0f.0";
    config.pci_write = argc==3? argv[2]:"0000:00:13.0";

    if (pthread_create(&socket_thread, NULL, factory_reset_thread, NULL) != 0) {
        log_wrapper(LOG_LEVEL_ERROR, "Failed to create socket server thread");
        return 1;
    }

    if (pthread_create(&uart_thread, NULL, ota_update_thread, NULL) != 0) {
        log_wrapper(LOG_LEVEL_ERROR, "Failed to create UART communication thread");
        return 1;
    }

    pthread_join(socket_thread, NULL);
    pthread_join(uart_thread, NULL);

    return 0;
}