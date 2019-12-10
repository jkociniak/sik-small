#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>

#include "err.h"
#include "utilities.h"
#include "dynamic_string.h"

#define QUEUE_LENGTH     128

int prepare_file_list(dyn_str* file_list, uint32_t* fl_len, char* const path_name) {
    *fl_len = 0;
    struct dirent* file;
    DIR* path;

    path = opendir(path_name);
    if (!path) {
        syserr_noexit("opendir");
        return -1;
    }

    errno = 0;
    file = readdir(path);
    if (!file && errno != 0) {
        syserr_noexit("readdir");
        return -1;
    }

    while (file) {
        char file_path[MAX_PATH_LEN+1];
        sprintf(file_path, "%s/%s", path_name, file->d_name);
        struct stat file_info;
        lstat(file_path, &file_info);

        if (S_ISREG(file_info.st_mode)) {
            char* pos = file->d_name;

            while (*pos) {
                if (!dyn_str_add(*file_list, *pos)) {
                    fprintf(stderr, "malloc for dynamic string failed\n");
                    return -1;
                }

                ++(*fl_len);
                ++pos;
            }

            dyn_str_add(*file_list, '|');
            ++(*fl_len);
        }

        file = readdir(path);
        if (!file && errno != 0) {
            syserr_noexit("readdir");
            return -1;
        }
    }

    if (closedir(path) < 0) {
        syserr_noexit("closedir");
        return -1;
    }

    if (*fl_len != 0)
        --(*fl_len); //truncate last separator

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3)
        fatal("Usage: %s <directory-name> [<port-number>]", argv[0]);

    int sock, msg_sock;
    struct sockaddr_in server_address;
    struct sockaddr_in client_address;
    socklen_t client_address_len;

    uint16_t port_num = DEFAULT_PORT_NUM;

    if (argc == 3)
        parse_port(argv[2], &port_num);

    sock = socket(PF_INET, SOCK_STREAM, 0); // creating IPv4 TCP socket
    if (sock < 0)
        syserr("socket");
    // after socket() call; we should close(sock) on any execution path;
    // since all execution paths exit immediately, sock would be closed when program terminates

    server_address.sin_family = AF_INET; // IPv4
    server_address.sin_addr.s_addr = htonl(INADDR_ANY); // listening on all interfaces
    server_address.sin_port = htons(port_num); // listening on port PORT_NUM

    // bind the socket to a concrete address
    if (bind(sock, (struct sockaddr *) &server_address, sizeof(server_address)) < 0)
        syserr("bind");

    // switch to listening (passive open)
    if (listen(sock, QUEUE_LENGTH) < 0)
        syserr("listen");

    printf("accepting client connections on port %hu\n", ntohs(server_address.sin_port));

    client_address_len = sizeof(client_address);

    while (true) {
        errno = 0;

        // get client connection from the socket
        msg_sock = accept(sock, (struct sockaddr *) &client_address, &client_address_len);

        if (msg_sock < 0) {
            syserr_noexit("accept");
            continue;
        }

        printf("connection accepted, waiting for request\n");

        while (true) {
            errno = 0;

            uint16_t req_type = 0;

            if (safe_read(msg_sock, &req_type, sizeof(req_type), "client") < 0) {
                safe_close(msg_sock);
                break;
            }

            req_type = ntohs(req_type);

            if (req_type == 1) {
                printf("received a request for file list\n");

                struct fl_info info;
                info.msg_start = htons(1);
                uint32_t fl_len;

                dyn_str file_list = dyn_str_init();

                if (!file_list) {
                    fprintf(stderr, "malloc for dynamic string failed\n");
                    safe_close(msg_sock);
                    break;
                }

                if (prepare_file_list(&file_list, &fl_len, argv[1])) {
                    safe_close(msg_sock);
                    break;
                }

                printf("successfully prepared file list\n");
                info.fl_len = htonl(fl_len);

                if (safe_write(msg_sock, &info, sizeof(struct fl_info), "client") < 0) {
                    dyn_str_delete(file_list);
                    safe_close(msg_sock);
                    break;
                }

                printf("successfully sent file list info\n");

                if (fl_len > 0) {
                    if (safe_write(msg_sock, file_list->str, fl_len, "client") <
                        0) {
                        dyn_str_delete(file_list);
                        safe_close(msg_sock);
                        break;
                    }

                    dyn_str_delete(file_list);
                    printf("successfully sent whole list, waiting for request\n");
                }
            } else if (req_type == 2) {
                printf("received a request for file, waiting for params\n");

                struct f_req_params f_info;

                if (safe_read(msg_sock, &f_info, sizeof(struct f_req_params), "client") <
                    0) {
                    safe_close(msg_sock);
                    break;
                }

                printf("successfully read request params, waiting for filename\n");

                f_info.begin_addr = ntohl(f_info.begin_addr);
                f_info.part_len = ntohl(f_info.part_len);
                f_info.name_len = ntohs(f_info.name_len);

                char file_name[MAX_PATH_LEN + 1]; // with '/0' at the end(?)

                if (f_info.name_len > MAX_PATH_LEN) {
                    printf("the name requested by client is too long\n");
                    safe_close(msg_sock);
                    break;
                }

                if (safe_read(msg_sock, file_name, f_info.name_len, "client") < 0) {
                    safe_close(msg_sock);
                    break;
                }

                printf("successfully read filename\n");

                file_name[f_info.name_len] = '\0';

                printf("checking params validity\n");

                uint16_t msg_start = 3;
                uint32_t second_param;
                bool valid_pathname = true;

                for (uint16_t pos = 0; pos < f_info.name_len; pos++) {
                    if (file_name[pos] == '/' || file_name[pos] == 0) {
                        printf("invalid filename\n");

                        msg_start = 2;
                        second_param = 1;
                        valid_pathname = false;
                        break;
                    }
                }

                // path_to_dir + '/' + file_name + '/0'
                size_t dir_path_len = strlen(argv[1]);
                size_t file_path_len = dir_path_len + f_info.name_len + 2;
                char file_path[file_path_len];

                strcpy(file_path, argv[1]);
                file_path[dir_path_len] = '/';
                strcpy(file_path + dir_path_len + 1, file_name);

                FILE * file;

                if (valid_pathname) {
                    file = fopen(file_path, "r");
                    if (!file) {
                        if (errno == ENOENT) {
                            printf("invalid filename\n");

                            msg_start = 2;
                            second_param = 1;
                            errno = 0;
                        } else {
                            syserr_noexit("fopen");
                            safe_close(msg_sock);
                            break;
                        }
                    } else {
                        struct stat f_stat;

                        if (lstat(file_path, &f_stat) < 0) {
                            syserr_noexit("stat");
                            safe_close(msg_sock);
                            break;
                        }

                        if (!S_ISREG(f_stat.st_mode)) {
                            printf("invalid filename\n");

                            msg_start = 2;
                            second_param = 1;
                        } else if (f_info.begin_addr > f_stat.st_size - 1) {
                            printf("invalid begin address\n");
                            msg_start = 2;
                            second_param = 2;
                        }
                    }
                }

                if (f_info.part_len == 0) {
                    printf("invalid part length\n");
                    msg_start = 2;
                    second_param = 3;
                }

                if (msg_start == 2) {
                    struct response_info r_info;
                    r_info.msg_start = htons(msg_start);
                    r_info.second_param = htonl(second_param);

                    if (safe_write(msg_sock, &r_info,
                                   sizeof(struct response_info), "client") < 0) {
                        safe_close(msg_sock);
                        break;
                    }

                    printf("successfully sent response info (refuse)\n");
                } else {
                    struct response_info r_info;
                    r_info.msg_start = htons(msg_start);

                    struct stat f_stat;

                    if (lstat(file_path, &f_stat) < 0) {
                        syserr_noexit("stat");
                        safe_close(msg_sock);
                        break;
                    }

                    if (f_info.begin_addr + f_info.part_len > f_stat.st_size)
                        second_param = f_stat.st_size - f_info.begin_addr;
                    else
                        second_param = f_info.part_len;

                    r_info.second_param = htonl(second_param);

                    if (safe_write(msg_sock, &r_info,
                                   sizeof(struct response_info), "client") < 0) {
                        safe_close(msg_sock);
                        break;
                    }

                    printf("successfully sent response info (accepted request)\n");

                    char buffer[MAX_CHUNK_SIZE];
                    uint64_t bytes_left = second_param;
                    uint32_t chunk_size;
                    bool err = false;

                    if (fseek(file, f_info.begin_addr, SEEK_SET)) {
                        syserr_noexit("fseek");
                        safe_close(msg_sock);
                        break;
                    }

                    printf("sending... bytes left: %lu\n", bytes_left);

                    do {
                        chunk_size = bytes_left < MAX_CHUNK_SIZE ? bytes_left
                                                                 : MAX_CHUNK_SIZE;

                        if (fread(&buffer, 1, chunk_size, file) != chunk_size) {
                            syserr_noexit("fread");
                            safe_close(msg_sock);
                            err = true;
                            break;
                        }

                        if (safe_write(msg_sock, &buffer, chunk_size, "client") < 0) {
                            safe_close(msg_sock);
                            err = true;
                            break;
                        }

                        printf("sending... bytes left: %lu\n", bytes_left);
                        bytes_left -= chunk_size;
                    } while (bytes_left > 0);

                    if (err)
                        break;

                    printf("successfully sent requested file fragment\n");
                }
            } else {
                printf("invalid request format\n");
            }
        }
    }

    return 0;
}