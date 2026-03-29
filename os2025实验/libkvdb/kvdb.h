#include <stddef.h>
#include <stdint.h>





// //Modify this struct.
// struct kvdb_t {
//     const char *path;
//     int fd;
// };

#define MAX_KEY 256
#define MAX_VAL 4096
#define MEMTABLE_MAX 1024

struct kvdb_node {
    char key[MAX_KEY];
    char val[MAX_VAL];
    struct kvdb_node *next;
};

struct kvdb_t {
    int fd;
    struct kvdb_node *memtable; // 有序链表
    int mem_cnt;
    int sst_seq;                // 当前SSTable序号
    char path[256];
};


int kvdb_open(struct kvdb_t *db, const char *path);
int kvdb_put(struct kvdb_t *db, const char *key, const char *value);
int kvdb_get(struct kvdb_t *db, const char *key, char *buf, size_t length);
int kvdb_close(struct kvdb_t *db);
