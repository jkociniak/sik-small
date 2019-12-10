#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

#include "utilities.h"
#include "err.h"

void safe_close(int sock) {
    printf("ending connection\n\n");

    if (close(sock) < 0)
        syserr_noexit("close");
}

int safe_read(int sock, void* buffer, size_t count, char* const who) {
    ssize_t len;
    uint64_t bytes_left = count;
    uint64_t offset = 0;
    uint32_t chunk_size;

    do {
        chunk_size = bytes_left < MAX_CHUNK_SIZE ? bytes_left : MAX_CHUNK_SIZE;
        len = read(sock, buffer + offset, chunk_size);

        if (len == 0) {
            printf("%s has disconnected\n", who);
            return -1;
        }

        if (len < 0) {
            syserr_noexit("reading from %s socket", who);
            return -1;
        }

        offset += len;
        bytes_left -= len;
    } while (bytes_left > 0);

    return 0;
}

int safe_write(int sock, void* buffer, size_t count, char* const who) {
    ssize_t len;
    uint64_t bytes_left = count;
    uint64_t offset = 0;
    uint32_t chunk_size;

    do {
        chunk_size = bytes_left < MAX_CHUNK_SIZE ? bytes_left : MAX_CHUNK_SIZE;
        len = write(sock, buffer + offset, chunk_size);

        if (len == 0) {
            printf("%s has disconnected\n", who);
            return -1;
        }

        if (len < 0) {
            syserr_noexit("writing to %s socket", who);
            return -1;
        }

        offset += len;
        bytes_left -= len;
    } while (bytes_left > 0);

    return 0;
}

void parse_port(char* const str, uint16_t* port_num) {
    errno = 0;
    char* endptr;
    unsigned long parsed = strtoul(str, &endptr, 10);

    if (parsed == 0) {
        endptr = str;
        while (*endptr != 0) {
            if (*endptr != 48)
                fatal("parsing of port number");

            ++endptr;
        }
    } else if (errno != 0) {
        syserr("strtoul");
    }

    *port_num = (uint16_t) parsed;
}

