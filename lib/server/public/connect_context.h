#include <aio.h>

struct connect_context {
    int messaging_socket;
    struct aiocb async_io_control_block;
    volatile unsigned char message_buffer[4096];
    bool is_pending_write;
    off_t file_offset;
    struct connect_context *prev;
    struct connect_context *next;
};