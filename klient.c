#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/stat.h>

#include "err.h"
#include "utilities.h"

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3)
        fatal("Usage: %s <server-name-or-ip4-address> [<port-number>]", argv[0]);

    int sock;
    struct addrinfo addr_hints;
    struct addrinfo *addr_result;

    int err;

    // 'converting' host/port in string to struct addrinfo
    memset(&addr_hints, 0, sizeof(struct addrinfo));
    addr_hints.ai_family = AF_INET; // IPv4
    addr_hints.ai_socktype = SOCK_STREAM;
    addr_hints.ai_protocol = IPPROTO_TCP;
    if (argc == 2)
        err = getaddrinfo(argv[1], "6543", &addr_hints, &addr_result);
    else
        err = getaddrinfo(argv[1], argv[2], &addr_hints, &addr_result);
    if (err == EAI_SYSTEM) { // system error
        syserr("getaddrinfo: %s", gai_strerror(err));
    }
    else if (err != 0) { // other error (host not found, etc.)
        fatal("getaddrinfo: %s", gai_strerror(err));
    }

    // initialize socket according to getaddrinfo results
    sock = socket(addr_result->ai_family, addr_result->ai_socktype, addr_result->ai_protocol);
    if (sock < 0)
        syserr("socket");

    // connect socket to the server
    if (connect(sock, addr_result->ai_addr, addr_result->ai_addrlen) < 0)
        syserr("connect");

    freeaddrinfo(addr_result);

    uint16_t request_type = ntohs(1);

    if (safe_write(sock, &request_type, sizeof(uint16_t), "server") < 0) {
        safe_close(sock);
        return 1;
    }

    struct fl_info info;

    if (safe_read(sock, &info, sizeof(struct fl_info), "server") < 0) {
        safe_close(sock);
        return 1;
    }

    info.fl_len = ntohl(info.fl_len);
    info.msg_start = ntohs(info.msg_start);

    if (info.msg_start != 1) {
        printf("invalid response from server\n");
        safe_close(sock);
        return 1;
    }

    printf("successfully read file list request info\n");

    if (info.fl_len == 0) {
        printf("file list is empty\n");
        safe_close(sock);
        return 1;
    }

    char* file_list = calloc(sizeof(char), info.fl_len);

    if (safe_read(sock, file_list, info.fl_len, "server") < 0) {
        safe_close(sock);
        free(file_list);
        return 1;
    }

    printf("successfully read file list\n\n");

    uint64_t word_number = 1;
    printf("%lu. ", word_number);
    for (int i = 0; i < info.fl_len; i++) {
        if (file_list[i] != '|')
            printf("%c", file_list[i]);
        else {
            printf("\n");
            ++word_number;
            printf("%lu. ", word_number);
        }
    }

    printf("\n\n");
    uint64_t chosen_word;

    do {
        printf("choose the file to send: ");

        scanf("%lu", &chosen_word);

        int c;
        while ((c = getchar()) != '\n' && c != EOF) { } //flush stdin

        if (chosen_word == 0) {
            printf("zero is not a valid file number!\n");
        } else if (chosen_word > word_number) {
            printf("too big file number!\n");
        }
    } while (chosen_word == 0 || chosen_word > word_number);

    word_number = 1;
    int i = 0;

    for (i = 0; i < info.fl_len; ++i) {
        if (word_number == chosen_word)
            break;

        if (file_list[i] == '|')
            word_number++;
    }

    struct f_req_params f_info;
    uint16_t name_len = 0;

    char name[MAX_PATH_LEN + 1];
    int j = 0;

    while (file_list[i] != '|' && i < info.fl_len) {
        name_len++;
        name[j] = file_list[i];
        j++;
        i++;
    }

    name[j] = '\0';

    free(file_list);

    uint32_t begin;
    uint32_t end;

    printf("enter begin address: ");
    scanf("%d", &begin);

    int c;
    while ((c = getchar()) != '\n' && c != EOF) { } //flush stdin


    do {
        printf("enter end address: ");
        scanf("%d", &end);

        int c;
        while ((c = getchar()) != '\n' && c != EOF) { } //flush stdin

        if (end < begin)
            printf("end must be bigger than begin, try again\n");
    } while (end < begin);

    request_type = htons(2);

    if (safe_write(sock, &request_type, sizeof(uint16_t), "server") < 0) {
        safe_close(sock);
        return 1;
    }

    f_info.name_len = htons(name_len);
    f_info.begin_addr = htonl(begin);
    f_info.part_len = htonl(end - begin);

    if (safe_write(sock, &f_info, sizeof(struct f_req_params), "server") < 0) {
        safe_close(sock);
        return 1;
    }

    printf("successfully sent file request info\n");

    if (safe_write(sock, name, name_len, "server") < 0) {
        safe_close(sock);
        return 1;
    }

    printf("successfully sent file name\n");

    struct response_info r_info;

    if (safe_read(sock, &r_info, sizeof(struct response_info), "server")) {
        safe_close(sock);
        return 1;
    }

    printf("successfully read response\n");

    r_info.msg_start = ntohs(r_info.msg_start);
    r_info.second_param = ntohl(r_info.second_param);

    if (r_info.msg_start == 2) {
        switch (r_info.second_param) {
            case 1:
                printf("refuse: wrong filename\n");
                break;

            case 2:
                printf("refuse: invalid begin address\n");
                break;

            case 3:
                printf("refuse: part length is 0\n");
                break;

            default:
                printf("invalid response from server\n");
        }
    } else if (r_info.msg_start == 3) {
        printf("request accepted, trying to download file\n");
        char path[MAX_PATH_LEN+5] = "tmp/";
        strcat(path, name);

        errno = 0;
        if (mkdir("./tmp", 0700) < 0) {
            if (errno != EEXIST) {
                syserr_noexit("mkdir");
                safe_close(sock);
                return 1;
            }

            errno = 0;
        }

        FILE* file = fopen(path, "r+");

        if (!file) {
            if (errno == ENOENT) {
                file = fopen(path, "w+");

                if (!file) {
                    syserr_noexit("fopen");
                    safe_close(sock);
                    return 1;
                }

                errno = 0;
            } else {
                syserr_noexit("fopen");
                safe_close(sock);
                return 1;
            }
        }

        printf("successfully opened file to write to\n");

        fseek(file, begin, SEEK_SET);

        char buffer[MAX_CHUNK_SIZE];
        uint64_t bytes_left = r_info.second_param;
        uint32_t chunk_size;

        printf("downloading... bytes left: %lu\n", bytes_left);

        do {
            chunk_size = bytes_left < MAX_CHUNK_SIZE ? bytes_left
                                                     : MAX_CHUNK_SIZE;

            if (safe_read(sock, &buffer, chunk_size, "server") < 0) {
                safe_close(sock);
                return 1;
            }

            if (fwrite(&buffer, 1, chunk_size, file) != chunk_size) {
                syserr_noexit("fwrite");
                safe_close(sock);
                return 1;
            }

            bytes_left -= chunk_size;
            printf("downloading... bytes left: %lu\n", bytes_left);
        } while (bytes_left > 0);

        printf("file successfully downloaded\n");
    } else {
        printf("invalid response from server\n");
        safe_close(sock);
        return 1;
    }

    safe_close(sock);

    return 0;
}


