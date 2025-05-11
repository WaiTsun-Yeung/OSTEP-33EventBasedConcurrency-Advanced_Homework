#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>

#ifdef CLIENT
    #include "client.h"
#else
    #include "server.h"
#endif

int main() {
    const int message_buffer_size = 1024;
    unsigned char message[message_buffer_size];
    (void)memset(message, 0, sizeof(message));
    const struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(rand() % 16383 + 49152),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
    };
    #ifdef CLIENT
        return run_client(&server_addr);
    #else
        return run_server(message_buffer_size, message, &server_addr);
    #endif
}
