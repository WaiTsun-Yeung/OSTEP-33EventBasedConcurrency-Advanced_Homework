struct connect_context {
    int messaging_socket;
    unsigned char message_buffer[1024];
    struct connect_context *prev;
    struct connect_context *next;
};