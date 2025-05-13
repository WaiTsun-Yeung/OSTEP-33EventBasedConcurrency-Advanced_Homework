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

int main(const int argc, const char *const argv[]) {
    const struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(rand() % 16383 + 49152),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
    };
    #ifdef CLIENT
        if (argc < 2){
            (void)fprintf(stderr, "Usage: %s <file_path>\n", argv[0]);
            return -1;
        }
        return run_client(&server_addr, argv[1]);
    #else
        (void)argc;
        (void)argv;
        return run_server(&server_addr);
    #endif
}
