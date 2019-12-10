#include <unistd.h>
#include <stdint.h>

#define MAX_PATH_LEN 256
#define MAX_CHUNK_SIZE (512*1024)
#define DEFAULT_PORT_NUM 6543

struct __attribute__((__packed__)) f_req_params {
    uint32_t begin_addr;
    uint32_t part_len;
    uint16_t name_len;
};

struct __attribute__((__packed__)) fl_info {
    uint16_t msg_start;
    uint32_t fl_len;
};

struct __attribute__((__packed__)) response_info {
    uint16_t msg_start;
    uint32_t second_param;
};

void safe_close(int sock);

int safe_read(int sock, void* buffer, size_t count, char* const who);

int safe_write(int sock, void* buffer, size_t count, char* const who);

void parse_port(char* const str, uint16_t* port_num);


