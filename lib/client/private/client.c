#include <stdio.h>
#include <time.h>
#include <stdbool.h>
#include <limits.h>
#include <threads.h>
#include <string.h>

#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>

static int connect_server(
    const int client_socket, const struct sockaddr_in *const server_addr
) {
    int connection_state = -1;
    errno = ECONNREFUSED;
    for (
        int connection_attempt = 0;
        connection_attempt < INT_MAX && connection_state == -1 
	    	&& errno == ECONNREFUSED;
        ++connection_attempt
    ) {
        const struct timespec sleep_time = {
            .tv_sec = 0,
            .tv_nsec = 250000000
        };
        (void)thrd_sleep(&sleep_time, NULL);
        connection_state = connect(
            client_socket, (const struct sockaddr *)server_addr, 
            sizeof(const struct sockaddr_in)
        );
    }
    return connection_state;
}
static int run_client_event_loop(
    int *restrict client_socket, 
    const struct sockaddr_in *restrict const server_addr
) {
    while (true) {
        time_t current_time = 0;
        const int bytes_received 
            = recv(*client_socket, &current_time, sizeof(current_time), 0);
        if (bytes_received == -1) {
            (void)fprintf(
                stderr, "recv() failed: %s\n", strerror(errno)
            );
            return -7;
        }
        if (!bytes_received){
            close(*client_socket);
            *client_socket = socket(AF_INET, SOCK_STREAM, 0);
            if (*client_socket == -1) {
                (void)fprintf(
                    stderr, "socket() failed: %s\n", strerror(errno)
                );
                return -3;
            }
            const int connection_state 
                = connect_server(*client_socket, server_addr);
            if (connection_state) return connection_state;
            continue;
        }
        if (
            printf(
                "Received time: %s", ctime(&current_time)
            ) < 0
        ) {
            (void)fprintf(
                stderr, "printf() failed: %s\n", strerror(errno)
            );
            return -8;
        }
    } 
}
static int get_acknowledgment(const int client_socket){
    const struct timeval timeout = { .tv_sec = 2, .tv_usec = 0 };
    if (
        setsockopt(
            client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, 
            sizeof(timeout)
        ) == -1
    ) {
        (void)fprintf(
            stderr, "setsockopt() failed: %s\n", strerror(errno)
        );
        return -10;
    } 
    bool is_file_opened = false;
    if (
        recv(
            client_socket, &is_file_opened, sizeof(is_file_opened), 0
        ) == -1
    ) {
        (void)fprintf(
            stderr, "recv() failed: %s\n", strerror(errno)
        );
        return -7;
    } 
    if (!is_file_opened){
        (void)fprintf(stderr, "File not opened on server\n");
        return -8;
    }
    const struct timeval null_timeout = { .tv_sec = 0, .tv_usec = 0 };
    if (
        setsockopt(
            client_socket, SOL_SOCKET, SO_RCVTIMEO, 
            (const char*)&null_timeout, sizeof(null_timeout)
        ) == -1
    ) {
        (void)fprintf(
            stderr, "setsockopt() failed: %s\n", strerror(errno)
        );
        return -10;
    } 
    return 0;
}
static int send_file_path(
    int *restrict client_socket,
    const struct sockaddr_in *restrict const server_addr,
    const char *restrict const file_path
){
    *client_socket = INT_MAX;
    errno = ECONNRESET;
    int sent_bytes = -1;
    for (
        int connection_attempt = 0; 
        errno == ECONNRESET && sent_bytes == -1 && connection_attempt < INT_MAX; 
        ++connection_attempt
    ) {
        if (*client_socket == INT_MAX) *client_socket 
            = socket(AF_INET, SOCK_STREAM, 0);
        if (*client_socket == -1) {
            (void)fprintf(stderr, "socket() failed: %s\n", strerror(errno));
            return -3;
        } 
        if (connect_server(*client_socket, server_addr) == -1) {
            (void)fprintf(stderr, "connect() failed: %s\n", strerror(errno));
            return -5;
        } 
        errno = ECONNRESET;
        sent_bytes = send(*client_socket, file_path, strlen(file_path), 0);
        if (sent_bytes > -1) return sent_bytes;
        if (errno != ECONNRESET) {
            (void)fprintf(stderr, "send() failed: %s\n", strerror(errno));
            return -6;
        }
        if (close(*client_socket) == -1) {
            (void)fprintf(stderr, "close() failed: %s\n", strerror(errno));
            return -9;
        }
        *client_socket = INT_MAX;
        errno = ECONNRESET;
    }
    return -1;
}
static int init_download(
    int *restrict client_socket,
    const struct sockaddr_in *restrict const server_addr,
    const char *restrict const file_path
){
    int app_status = send_file_path(client_socket, server_addr, file_path);
    if (app_status > -1) app_status = get_acknowledgment(*client_socket);
    return app_status;
}
int run_client(
    const struct sockaddr_in *const server_addr, const char *const file_path
){
    int client_socket = -1;
    int app_status = init_download(&client_socket, server_addr, file_path);
    if (!app_status) 
        app_status = run_client_event_loop(&client_socket, server_addr);
    if (close(client_socket) == -1) {
        (void)fprintf(stderr, "close() failed: %s\n", strerror(errno));
        app_status = -9;
    }
    return app_status;
}