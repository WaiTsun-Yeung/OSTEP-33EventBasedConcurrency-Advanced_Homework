#include <stdbool.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
static int init_server(
    const int listening_socket, const struct sockaddr_in *const server_addr
) {
    if (
        bind(
            listening_socket, (const struct sockaddr *)server_addr, 
            sizeof(const struct sockaddr_in)
        ) == -1
    ) {
        (void)fprintf(stderr, "bind() failed: %s\n", strerror(errno));
        return -4;
    }
    if (listen(listening_socket, SHRT_MAX) == -1) {
        (void)fprintf(stderr, "listen() failed: %s\n", strerror(errno));
        return -7;
    }
    return 0;
}

int run_server(
    const int message_buffer_size, unsigned char message[message_buffer_size],
    const struct sockaddr_in *const server_addr
){
    const int listening_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (listening_socket == -1) {
        (void)fprintf(stderr, "socket() failed: %s\n", strerror(errno));
        return -2;
    }
    int app_status = init_server(listening_socket, server_addr);
    if (!app_status) {
        int messaging_socket = accept(listening_socket, NULL, NULL);
        if (messaging_socket == -1){ 
            (void)fprintf(stderr, "accept() failed: %s\n", strerror(errno));
            app_status = messaging_socket;
        } else{
            while (true) {
                const time_t current_time = time(NULL);
                if (current_time == -1) {
                    (void)fprintf(stderr, "time() failed: %s\n", strerror(errno));
                    app_status = -6;
                    break;
                }
                if (
                    send(
                        messaging_socket, 
                        memcpy(message, &current_time, sizeof(current_time)), 
                        sizeof(current_time), 0
                    ) == -1
                ){
                    if (errno != ECONNRESET) {
                        (void)fprintf(stderr, "send() failed: %s\n", strerror(errno));
                        app_status = -8;
                        break;
                    }
                    messaging_socket = accept(
                        listening_socket, NULL, NULL
                    );
                    if (messaging_socket > -1) continue; 
                    (void)fprintf(stderr, "accept() failed: %s\n", strerror(errno));
                    app_status = -5;
                    break;
                }
            }
        } 
    }
    if (close(listening_socket) == -1) {
        (void)fprintf(stderr, "close) failed: %s\n", strerror(errno));
        app_status = -3;
    }
    return app_status;
}
