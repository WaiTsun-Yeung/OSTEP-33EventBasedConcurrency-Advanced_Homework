#include <stdio.h>
#include <time.h>
#include <stdbool.h>
#include <limits.h>
#include <threads.h>
#include <string.h>

#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
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
int run_client(const struct sockaddr_in *const server_addr) {
    int app_status = 0;
    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1) {
        (void)fprintf(stderr, "socket() failed: %s\n", strerror(errno));
        return -3;
    } 
    if (connect_server(client_socket, server_addr) == -1) {
        (void)fprintf(stderr, "connect() failed: %s\n", strerror(errno));
        app_status = -5;
    } else app_status = run_client_event_loop(&client_socket, server_addr);
    if (close(client_socket) == -1) {
        (void)fprintf(stderr, "close() failed: %s\n", strerror(errno));
        app_status = -9;
    }
    return app_status;
}