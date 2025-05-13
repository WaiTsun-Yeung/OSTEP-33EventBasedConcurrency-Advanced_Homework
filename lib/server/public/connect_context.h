struct connect_context {
    int messaging_socket;
    int file;
    unsigned char message_buffer[4096];
    struct connect_context *prev;
    struct connect_context *next;
};