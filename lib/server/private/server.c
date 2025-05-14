#include <stdbool.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "connect_context.h"

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
static int accept_new_connection(
    const int listening_socket, struct connect_context **const connection_list
) {
    int messaging_socket = accept(listening_socket, NULL, NULL);
    if (messaging_socket > -1) {
        const int messaging_socket_flags = fcntl(messaging_socket, F_GETFL);
        if(messaging_socket_flags == -1){
            (void)fprintf(stderr, "fcntl() failed: %s\n", strerror(errno));
            return -9;
        }
        if(
            fcntl(
                messaging_socket, F_SETFL, 
                messaging_socket_flags | O_NONBLOCK
            ) == -1
        ){
            (void)fprintf(stderr, "fcntl() failed: %s\n", strerror(errno));
            return -10;
        }
        struct connect_context *new_connection = malloc(
            sizeof(struct connect_context)
        );
        if (!new_connection) {
            (void)fprintf(stderr, "malloc() failed: \n");
            return -11;
        }
        new_connection->messaging_socket = messaging_socket;
        new_connection->file = -1;
        new_connection->prev = NULL;
        if (*connection_list){
            new_connection->next = *connection_list;
            (*connection_list)->prev = new_connection;
            *connection_list = new_connection;
        } else {
            new_connection->next = NULL;
            *connection_list = new_connection;
        }
    } else if (errno != EAGAIN && errno != EWOULDBLOCK)  
        (void)fprintf(stderr, "accept() failed: %s\n", strerror(errno));
    return 0;
}
static void remove_connection(
    struct connect_context **const connection_list, 
    struct connect_context *const connection
) {
    close(connection->messaging_socket);
    if (connection->file != -1) close(connection->file);
    struct connect_context *const prev_connection = connection->prev;
    struct connect_context *const next_connection = connection->next;
    if (next_connection) next_connection->prev = prev_connection;
    if (connection == *connection_list) *connection_list = next_connection;
    else prev_connection->next = next_connection;
    free(connection);
}
static void send_message(
    struct connect_context **const connection_list,
    struct connect_context *const connection
) {
    const time_t current_time = time(NULL);
    if (current_time == -1) {
        (void)fprintf(stderr, "time() failed: %s\n", strerror(errno));
        return;
    }
    //TODO: aio read file with seek index, then add callback to send 
    //when that returns.
    if (
        send(
            connection->messaging_socket, 
            memcpy(
                connection->message_buffer, &current_time, sizeof(current_time)
            ), 
            sizeof(current_time), 
            0
        ) > -1
    ) return;
    remove_connection(connection_list, connection);
    if (errno == ECONNRESET) return; 
    (void)fprintf(
        stderr, "send() failed: fd: %d: %s\n", 
        connection->messaging_socket, strerror(errno)
    );
    return;
}
static void open_file(
    struct connect_context **const connection_list,
    struct connect_context *const connection
){
    connection->file = open((const char *)connection->message_buffer, O_RDONLY);
    bool is_file_opened = false;
    if (connection->file > -1) {
        is_file_opened = true;
        (void)printf(
            "File descriptor %d opened for file: %s\n", connection->file, 
            (const char *)connection->message_buffer
        );
        if (
            send(
                connection->messaging_socket, &is_file_opened, 
                sizeof(is_file_opened), 0
            ) > -1
        ){
            (void)memset(
                connection->message_buffer, 0, 
                strlen((const char *)connection->message_buffer)
            );
            return;
        } else (void)fprintf(
            stderr, "send() failed: fd: %d: %s\n", connection->messaging_socket, 
            strerror(errno)
        );
    } else {
        (void)fprintf(
            stderr, "open() failed: fd: %d: %s\n", connection->messaging_socket, 
            strerror(errno)
        );
        if (
            send(
                connection->messaging_socket, &is_file_opened, 
                sizeof(is_file_opened), 0
            ) == -1
        ) (void)fprintf(
            stderr, "send() failed: fd: %d: %s\n", connection->messaging_socket, 
            strerror(errno)
        );
        (void)printf(
            "File descriptor %d closed for file: %s\n", 
            connection->messaging_socket, 
            (const char *)connection->message_buffer
        );
    }
    remove_connection(connection_list, connection);
    return;
}
static void read_message(
    struct connect_context **const connection_list,
    struct connect_context *const connection
) {
    const ssize_t bytes_read = recv(
        connection->messaging_socket, 
        connection->message_buffer, sizeof(connection->message_buffer), 0
    );
    if (bytes_read == -1) {
        (void)fprintf(
            stderr, "recv() failed: fd: %d: %s\n", 
            connection->messaging_socket, strerror(errno)
        );
        remove_connection(connection_list, connection);
        return;
    }
    connection->message_buffer[bytes_read] = '\0';
    open_file(connection_list, connection);
    return;
}
static int gather_active_sockets(
    struct connect_context *restrict connection_list,
    fd_set *restrict const writable_sockets,
    fd_set *restrict const readable_sockets,
    int *restrict const active_sockets_count
) {
    FD_ZERO(writable_sockets);
    FD_ZERO(readable_sockets);
    struct connect_context *connection = connection_list;
    for (
        int i = 0; 
        connection && i < FD_SETSIZE; ++i, connection = connection->next
    ) if (connection->file == -1)
        FD_SET(connection->messaging_socket, readable_sockets);
    else FD_SET(connection->messaging_socket, writable_sockets);
    struct timeval timeout = { .tv_sec = 0, .tv_usec = 0 };
    *active_sockets_count = select(
        FD_SETSIZE, readable_sockets, writable_sockets, NULL, &timeout
    );
    if (*active_sockets_count == -1) {
        (void)fprintf(stderr, "select() failed: %s\n", strerror(errno));
        return -11;
    }
    return 0;
}
static void free_connections(struct connect_context *connection_list) {
    for (int i = 0; i < FD_SETSIZE && connection_list; ++i) {
        struct connect_context *const next_connection = connection_list->next;
        close(connection_list->messaging_socket);
        if (connection_list->file != -1) close(connection_list->file);
        free(connection_list);
        connection_list = next_connection;
    }
}
static void write_to_sockets(
    struct connect_context **const connection_list,
    fd_set *const writable_sockets
) {
    struct connect_context *connection = *connection_list;
    struct connect_context *next_connection 
        = connection ? connection->next : NULL;
    for (
        int i = 0; 
        connection && i < FD_SETSIZE; ++i, connection = next_connection
    ) if (FD_ISSET(connection->messaging_socket, writable_sockets)) {
        next_connection = connection->next;
        send_message(connection_list, connection);
    }
}
static void read_from_sockets(
    struct connect_context **const connection_list,
    fd_set *const readable_sockets
) {
    struct connect_context *connection = *connection_list;
    struct connect_context *next_connection 
        = connection ? connection->next : NULL;
    for (
        int i = 0; 
        connection && i < FD_SETSIZE; ++i, connection = next_connection
    ) if (FD_ISSET(connection->messaging_socket, readable_sockets)) {
        next_connection = connection->next;
        read_message(connection_list, connection);
    }
}
static int run_server_event_loop(const int listening_socket) {
    int app_status = 0;
    struct connect_context *connection_list = NULL;
    while (true) {
        app_status = accept_new_connection(
            listening_socket, &connection_list
        );
        if (app_status) break;
        fd_set writable_sockets;
        fd_set readable_sockets;
        int active_sockets_count = 0;
        app_status = gather_active_sockets(
            connection_list, &writable_sockets, &readable_sockets, 
            &active_sockets_count
        );
        if (app_status) break;
        if (active_sockets_count == 0) continue;
        read_from_sockets(&connection_list, &readable_sockets);
        write_to_sockets(&connection_list, &writable_sockets);
    }
    free_connections(connection_list);
    return app_status;
}

int run_server(const struct sockaddr_in *const server_addr){
    const int listening_socket 
        = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listening_socket == -1) {
        (void)fprintf(stderr, "socket() failed: %s\n", strerror(errno));
        return -2;
    }
    int app_status = init_server(listening_socket, server_addr);
    if (!app_status) app_status = run_server_event_loop(listening_socket);
    if (close(listening_socket) == -1) {
        (void)fprintf(stderr, "close) failed: %s\n", strerror(errno));
        app_status = -3;
    }
    return app_status;
}
