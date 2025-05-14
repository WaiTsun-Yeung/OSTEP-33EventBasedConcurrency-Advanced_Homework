#include <aio.h>

struct connect_context {
    int messaging_socket;
    int file;
    struct aiocb async_io_control_block;
    volatile unsigned char message_buffer[4096];
    struct connect_context *prev;
    struct connect_context *next;
};