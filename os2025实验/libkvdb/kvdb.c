// #include <kvdb.h>

// int kvdb_open(struct kvdb_t *db, const char *path) {
//     return -1;
// }

// int kvdb_put(struct kvdb_t *db, const char *key, const char *value) {
//     return -1;
// }

// int kvdb_get(struct kvdb_t *db, const char *key, char *buf, size_t length) {
//     return -1;
// }

// int kvdb_close(struct kvdb_t *db) {
//     return -1;
// }


#include "kvdb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <errno.h>

#define LOG_MAGIC 0x6b76646c  // 'kvdl'
#define LOG_ENTRY_SIZE (4+4+4+MAX_KEY+MAX_VAL) // magic+klen+vlen+key+val
#define LOG_AREA_SIZE (LOG_ENTRY_SIZE*1024)    // 预留日志区大小



static int lock_fd(int fd) { return flock(fd, LOCK_EX); }
static int unlock_fd(int fd) { return flock(fd, LOCK_UN); }



// MemTable插入/更新（有序链表，便于flush时顺序写盘）
static void memtable_set(struct kvdb_t *db, const char *key, const char *val) {
    struct kvdb_node **pp = &db->memtable;
    while (*pp && strcmp((*pp)->key, key) < 0) pp = &(*pp)->next;
    if (*pp && strcmp((*pp)->key, key) == 0) {
        strncpy((*pp)->val, val, MAX_VAL);
        return;
    }
    struct kvdb_node *node = malloc(sizeof(struct kvdb_node));
    strncpy(node->key, key, MAX_KEY);
    strncpy(node->val, val, MAX_VAL);
    node->next = *pp;
    *pp = node;
    db->mem_cnt++;
}

// MemTable查找
static struct kvdb_node* memtable_get(struct kvdb_t *db, const char *key) {
    struct kvdb_node *node = db->memtable;
    while (node) {
        int cmp = strcmp(node->key, key);
        if (cmp == 0) return node;
        if (cmp > 0) break;
        node = node->next;
    }
    return NULL;
}

// flush MemTable到SSTable
static int flush_memtable(struct kvdb_t *db) {
    if (db->mem_cnt == 0) return 0;
    char sstfile[300];
    snprintf(sstfile, sizeof(sstfile), "%s.sst.%d", db->path, ++db->sst_seq);
    int fd = open(sstfile, O_WRONLY|O_CREAT|O_TRUNC, 0666);
    if (fd < 0) return -1;
    struct kvdb_node *node = db->memtable;
    while (node) {
        uint32_t klen = strlen(node->key), vlen = strlen(node->val);
        write(fd, &klen, 4); write(fd, &vlen, 4);
        write(fd, node->key, klen); write(fd, node->val, vlen);
        node = node->next;
    }
    fsync(fd);
    close(fd);
    // 清空MemTable
    node = db->memtable;
    while (node) {
        struct kvdb_node *tmp = node;
        node = node->next;
        free(tmp);
    }
    db->memtable = NULL;
    db->mem_cnt = 0;
    return 0;
}

// 查找所有SSTable（新到旧）
static int sstable_get(struct kvdb_t *db, const char *key, char *buf, size_t length) {
    char sstfile[300];
    for (int seq = db->sst_seq; seq > 0; --seq) {
        snprintf(sstfile, sizeof(sstfile), "%s.sst.%d", db->path, seq);
        int fd = open(sstfile, O_RDONLY);
        if (fd < 0) continue;
        uint32_t klen, vlen;
        char kbuf[MAX_KEY], vbuf[MAX_VAL];
        while (read(fd, &klen, 4) == 4 && read(fd, &vlen, 4) == 4) {
            if (klen == 0 || klen >= MAX_KEY || vlen == 0 || vlen >= MAX_VAL) break;
            if (read(fd, kbuf, klen) != (ssize_t)klen) break;
            if (read(fd, vbuf, vlen) != (ssize_t)vlen) break;
            kbuf[klen] = 0; vbuf[vlen] = 0;
            if (strcmp(kbuf, key) == 0) {
                size_t cpy = (vlen < length-1) ? vlen : length-1;
                memcpy(buf, vbuf, cpy); buf[cpy] = 0;
                close(fd);
                return cpy;
            }
        }
        close(fd);
    }
    return -1;
}

// open时恢复SSTable序号
static int detect_sst_seq(const char *path) {
    int seq = 0;
    char sstfile[300];
    while (1) {
        snprintf(sstfile, sizeof(sstfile), "%s.sst.%d", path, seq+1);
        if (access(sstfile, F_OK) == 0) seq++;
        else break;
    }
    return seq;
}


// 写日志区
static int wal_append(int fd, const char *key, const char *val) {
    uint32_t magic = LOG_MAGIC;
    uint32_t klen = strlen(key), vlen = strlen(val);
    if (klen == 0 || klen >= MAX_KEY || vlen >= MAX_VAL) return -1;
    lseek(fd, 0, SEEK_SET); // 日志区从文件头开始
    // 找到第一个空日志槽
    for (int i = 0; i < 1024; ++i) {
        off_t pos = i * LOG_ENTRY_SIZE;
        uint32_t m = 0;
        pread(fd, &m, 4, pos);
        if (m != LOG_MAGIC) {
            // 写入日志
            pwrite(fd, &magic, 4, pos);
            pwrite(fd, &klen, 4, pos+4);
            pwrite(fd, &vlen, 4, pos+8);
            pwrite(fd, key, klen, pos+12);
            pwrite(fd, val, vlen, pos+12+klen);
            fsync(fd);
            return i;
        }
    }
    return -1; // 日志区满
}

// 标记日志已完成
static void wal_clear(int fd, int idx) {
    uint32_t zero = 0;
    pwrite(fd, &zero, 4, idx*LOG_ENTRY_SIZE); // magic=0
    fsync(fd);
}

// 追加数据区
static int data_append(int fd, const char *key, const char *val) {
    uint32_t klen = strlen(key), vlen = strlen(val);
    off_t pos = LOG_AREA_SIZE;
    lseek(fd, 0, SEEK_END);
    if (pos < lseek(fd, 0, SEEK_CUR)) pos = lseek(fd, 0, SEEK_CUR);
    lseek(fd, 0, SEEK_END);
    write(fd, &klen, 4); write(fd, &vlen, 4);
    write(fd, key, klen); write(fd, val, vlen);
    fsync(fd);
    return 0;
}

// open 时重放日志
static void wal_replay(int fd) {
    for (int i = 0; i < 1024; ++i) {
        off_t pos = i * LOG_ENTRY_SIZE;
        uint32_t magic = 0, klen = 0, vlen = 0;
        pread(fd, &magic, 4, pos);
        if (magic != LOG_MAGIC) continue;
        pread(fd, &klen, 4, pos+4);
        pread(fd, &vlen, 4, pos+8);
        if (klen == 0 || klen >= MAX_KEY || vlen >= MAX_VAL) continue;
        char kbuf[MAX_KEY], vbuf[MAX_VAL];
        pread(fd, kbuf, klen, pos+12); kbuf[klen] = 0;
        pread(fd, vbuf, vlen, pos+12+klen); vbuf[vlen] = 0;
        // 追加到数据区
        lseek(fd, 0, SEEK_END);
        write(fd, &klen, 4); write(fd, &vlen, 4);
        write(fd, kbuf, klen); write(fd, vbuf, vlen);
        fsync(fd);
        wal_clear(fd, i);
    }
}




// 数据文件格式: [key_len:4][val_len:4][key][val] 顺序追加
int kvdb_open(struct kvdb_t *db, const char *path) {
        if (!db || !path) return -1;
    db->fd = open(path, O_RDWR|O_CREAT, 0666);
    if (db->fd < 0) return -1;
    strncpy(db->path, path, sizeof(db->path)-1);
    db->memtable = NULL;
    db->mem_cnt = 0;
    db->sst_seq = detect_sst_seq(path);
    return 0;
}

int kvdb_put(struct kvdb_t *db, const char *key, const char *value) {
    if (!db || !key || !value) return -1;
    if (lock_fd(db->fd) != 0) return -1;
    memtable_set(db, key, value);
    if (db->mem_cnt >= MEMTABLE_MAX) flush_memtable(db);
    unlock_fd(db->fd);
    return 0;
}

int kvdb_get(struct kvdb_t *db, const char *key, char *buf, size_t length) {
    if (!db || !key || !buf || length == 0) return -1;
    if (lock_fd(db->fd) != 0) return -1;
    struct kvdb_node *node = memtable_get(db, key);
    if (node) {
        size_t vlen = strlen(node->val);
        size_t cpy = (vlen < length-1) ? vlen : length-1;
        memcpy(buf, node->val, cpy); buf[cpy] = 0;
        unlock_fd(db->fd);
        return cpy;
    }
    int ret = sstable_get(db, key, buf, length);
    unlock_fd(db->fd);
    return ret;
}

int kvdb_close(struct kvdb_t *db) {
    if (!db) return -1;
    if (lock_fd(db->fd) != 0) return -1;
    flush_memtable(db);
    unlock_fd(db->fd);
    int ret = close(db->fd);
    return ret == 0 ? 0 : -1;
}

