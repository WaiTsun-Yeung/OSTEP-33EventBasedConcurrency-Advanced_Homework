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

#include <signal.h>

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
                messaging_socket, F_SETFL, messaging_socket_flags | O_NONBLOCK
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
        new_connection->async_io_control_block.aio_fildes = -1;
        new_connection->async_io_control_block.aio_buf 
            = new_connection->message_buffer;
        new_connection->async_io_control_block.aio_nbytes 
            = sizeof(new_connection->message_buffer);
        new_connection->async_io_control_block.aio_offset = 0;
        new_connection->async_io_control_block.aio_sigevent.sigev_notify 
            = SIGEV_SIGNAL;
        new_connection->async_io_control_block.aio_sigevent.sigev_signo 
            = SIGUSR1;
        new_connection->async_io_control_block.aio_sigevent
            .sigev_value.sival_ptr = new_connection;
        new_connection->async_io_control_block.aio_sigevent
            .sigev_notify_function = NULL;
        new_connection->async_io_control_block.aio_sigevent
            .sigev_notify_attributes = NULL;
        new_connection->async_io_control_block.aio_lio_opcode = LIO_READ;
        new_connection->async_io_control_block.aio_reqprio = 0;
        new_connection->is_pending_write = false;
        new_connection->file_offset 
            = new_connection->async_io_control_block.aio_offset;
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
    if (connection->async_io_control_block.aio_fildes!= -1) 
        close(connection->async_io_control_block.aio_fildes);
    struct connect_context *const prev_connection = connection->prev;
    struct connect_context *const next_connection = connection->next;
    if (next_connection) next_connection->prev = prev_connection;
    if (connection == *connection_list) *connection_list = next_connection;
    else prev_connection->next = next_connection;
    free(connection);
}
static void clear_buffer(struct connect_context *const connection) {
    for (
        volatile unsigned char *byte = connection->message_buffer; 
        byte < connection->message_buffer + sizeof(connection->message_buffer); 
        ++byte
    ) *byte = '\0';
}
static void send_message(
    struct connect_context **const connection_list,
    struct connect_context *const connection
) {
    connection->is_pending_write = false;
    unsigned char message_buffer[sizeof(connection->message_buffer)];
    unsigned char *byte = message_buffer;
    for (
        const volatile unsigned char *message_byte = connection->message_buffer;
        message_byte < connection->message_buffer 
            + sizeof(connection->message_buffer);
        ++message_byte, ++byte
    ) *byte = *message_byte;
    if (
        send(
            connection->messaging_socket, message_buffer, 
            sizeof(message_buffer), 0
        ) > -1
    ) {
        clear_buffer(connection);
        connection->async_io_control_block.aio_offset
            = connection->file_offset + sizeof(message_buffer);
        connection->file_offset = connection->async_io_control_block.aio_offset;
        if (aio_read(&connection->async_io_control_block) == -1){ 
            if (errno != EOVERFLOW) (void)fprintf(
                stderr, "aio_read() failed: %s\n", strerror(errno)
            );
        } else return;
    } if (errno != ECONNRESET) (void)fprintf(
        stderr, "send() failed: fd: %d: %s\n", 
        connection->messaging_socket, strerror(errno)
    );
    remove_connection(connection_list, connection);
}
static void fail_open_file(
    struct connect_context *const connection, const char *const message_buffer
) {
    (void)fprintf(
        stderr, "open() failed: fd: %d: %s\n", connection->messaging_socket, 
        strerror(errno)
    );
    const bool is_file_opened = false;
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
        connection->messaging_socket, message_buffer
    );
}
static void open_file(
    struct connect_context **const connection_list,
    struct connect_context *const connection
){
    unsigned char message_buffer[sizeof(connection->message_buffer)];
    unsigned char *byte = message_buffer;
    for (
        const volatile unsigned char *message_byte = connection->message_buffer;
        message_byte 
            < connection->message_buffer + sizeof(connection->message_buffer);
        ++message_byte, ++byte
    ) if (*message_byte == '\0') {
        *byte = '\0';
        break;
    } else *byte = *message_byte;
    connection->async_io_control_block.aio_fildes 
        = open((const char *)message_buffer, O_RDONLY);
    if (connection->async_io_control_block.aio_fildes > -1) {
        const bool is_file_opened = true;
        (void)printf(
            "File descriptor %d opened for file: %s\n", 
            connection->async_io_control_block.aio_fildes, 
            (const char *)message_buffer
        );
        if (
            send(
                connection->messaging_socket, &is_file_opened, 
                sizeof(is_file_opened), 0
            ) > -1
        ){
            clear_buffer(connection);
            if (aio_read(&connection->async_io_control_block) == -1) 
                (void)fprintf(
                    stderr, "aio_read() failed: %s\n", strerror(errno)
                );
            else return;
        } else (void)fprintf(
            stderr, "send() failed: fd: %d: %s\n", connection->messaging_socket, 
            strerror(errno)
        );
    } else fail_open_file(connection, (const char *)message_buffer);
    remove_connection(connection_list, connection);
    return;
}
static void read_message(
    struct connect_context **const connection_list,
    struct connect_context *const connection
) {
    unsigned char message_buffer[sizeof(connection->message_buffer)];
    const ssize_t bytes_read = recv(
        connection->messaging_socket, message_buffer, sizeof(message_buffer), 0
    );
    if (bytes_read == -1) {
        (void)fprintf(
            stderr, "recv() failed: fd: %d: %s\n", 
            connection->messaging_socket, strerror(errno)
        );
        remove_connection(connection_list, connection);
        return;
    }
    volatile unsigned char *connection_message_byte 
        = connection->message_buffer;
    for (
        const unsigned char *byte = message_buffer;
        byte < message_buffer + bytes_read; 
        ++byte, ++connection_message_byte
    ) *connection_message_byte = *byte;
    *connection_message_byte = '\0';
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
    ) if (connection->async_io_control_block.aio_fildes == -1)
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
        if (connection_list->async_io_control_block.aio_fildes != -1) 
            close(connection_list->async_io_control_block.aio_fildes);
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
        if (connection->is_pending_write){
            if (!*(volatile unsigned char*)connection->message_buffer){
                remove_connection(connection_list, connection);
                continue;
            }
            send_message(connection_list, connection);
        }
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
static void aioSigHandler(
    const int sig, siginfo_t *const si, void *const ucontext
){
    (void)ucontext;
    (void)sig;
    if (sig == SIGUSR1 && si->si_code == SI_ASYNCIO) {
        struct connect_context *const connection 
            = (struct connect_context *)si->si_value.sival_ptr;
        connection->is_pending_write = true;
    }
}
static int init_signal_handlers(struct sigaction *sa){
    sa->sa_flags = SA_RESTART;
    if (sigemptyset(&sa->sa_mask) == -1) {
        (void)fprintf(stderr, "sigemptyset() failed: %s\n", strerror(errno));
        return -1;
    }
    sa->sa_flags = SA_RESTART | SA_SIGINFO;
    sa->sa_sigaction = aioSigHandler;
    if (sigaction(SIGUSR1, sa, NULL) == -1) {
        (void)fprintf(stderr, "sigaction() failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}
int run_server(const struct sockaddr_in *const server_addr){
    struct sigaction sa;
    if (init_signal_handlers(&sa)) return -1;
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
