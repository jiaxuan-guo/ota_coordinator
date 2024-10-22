#include "ota_coordinator.h"
#include <cutils/sockets.h>

#define BUFFER_SIZE 256
#define OTA_SOCKET "ota_socket"

void *factory_reset() {
    char buffer[BUFFER_SIZE];
    int service_sock;
    char log[256];

    log_wrapper(LOG_LEVEL_INFO, "Getting ota_socket...\n");
    service_sock = android_get_control_socket(OTA_SOCKET);
    if (service_sock == -1) {
        log_wrapper(LOG_LEVEL_ERROR, "failed to open ota_socket");
        return NULL;
    }
    log_wrapper(LOG_LEVEL_INFO, "Open ota_socket successfully\n");

    if (fcntl(service_sock, F_SETFD, FD_CLOEXEC) == -1) {
        log_wrapper(LOG_LEVEL_ERROR, "failed to set FD_CLOEXEC");
        close(service_sock);
        return NULL;
    }

    if (listen(service_sock, 1) == -1) {
        log_wrapper(LOG_LEVEL_ERROR, "failed to listen on socket");
        close(service_sock);
        return NULL;
    }
    log_wrapper(LOG_LEVEL_INFO, "Server is listening...\n");

    int client_sock = accept4(service_sock, NULL, NULL, SOCK_CLOEXEC);
    if (client_sock == -1) {
        log_wrapper(LOG_LEVEL_ERROR, "failed to accept on socket");
        close(service_sock);
        return NULL;
    }
    log_wrapper(LOG_LEVEL_INFO, "Connection accepted\n");

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(client_sock, buffer, BUFFER_SIZE, 0);
        if (bytes_received < 0) {
            log_wrapper(LOG_LEVEL_ERROR, "bytes_received is less than 0!\n");
        } else if (bytes_received == 0) {
            log_wrapper(LOG_LEVEL_ERROR, "Client disconnected!\n");
            break;
        } else {
            buffer[bytes_received] = '\0';
            snprintf(log, sizeof(log), "bytes_received: %d, Received message: %s\n", bytes_received, buffer);
            log_wrapper(LOG_LEVEL_INFO, log);
            if (strncmp(buffer, "start_factory_reset", sizeof("start_factory_reset")-1) == 0) {
                log_wrapper(LOG_LEVEL_INFO, "start handle_start_factory_reset\n");
                handle_start_factory_reset();
            }
        }
    }

    close(client_sock);
    close(service_sock);
    return NULL;
}