#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <ctype.h>
#include "fat32.h"

// BMP文件信息结构
typedef struct {
    uint32_t data_offset; 
    uint32_t width;          // 宽度
    uint32_t height;         // 高度
    uint32_t file_size;     
    uint16_t bpp; //每像素位数
    uint32_t row_bytes;      // 每行字节数
} bmp_info_t;

int valid_name(const char *name);
int is_directory_cluster(uint8_t *cluster_data, uint32_t cluster_size);
bmp_info_t extract_bmp_info(uint8_t *bmp_data);
void *map_disk(const char *fname);
void recover_func(struct fat32hdr *hdr);
char *get_filename(struct fat32dent *entries, int start_idx, int count);
void recover_bmp_file(struct fat32hdr *hdr, struct fat32dent *entry, const char *filename);
char *get_sha1sum(void *data, size_t size);
double count_similarity(uint8_t *cur, uint8_t *next, bmp_info_t *info, uint32_t cluster_size);
uint32_t find_next_cluster(struct fat32hdr *hdr, uint8_t *cur_data, bmp_info_t *info, 
                           uint32_t current_cluster, uint32_t total_clusters, 
                           uint32_t cluster_size, uint8_t *used);
// 长文件名常量
#define ATTR_LONG_NAME 0x0F
#define LFN_LAST_ENTRY 0x40
#define BMP_HEADER_SIGNATURE 0x4D42  // "BM"
#define get_start(hdr) \
    ((hdr)->BPB_RsvdSecCnt + ((hdr)->BPB_NumFATs * (hdr)->BPB_FATSz32))

#define get_data(hdr, cluster) \
    ((uint8_t *)(hdr) + (get_start(hdr) + \
    ((cluster) - 2) * (hdr)->BPB_SecPerClus) * (hdr)->BPB_BytsPerSec)

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s fs-image\n", argv[0]);
        exit(1);
    }

    setbuf(stdout, NULL);

    assert(sizeof(struct fat32hdr) == 512); // defensive

    // map disk image to memory
    struct fat32hdr *hdr = map_disk(argv[1]);

    recover_func(hdr);

    // file system traversal
    munmap(hdr, hdr->BPB_TotSec32 * hdr->BPB_BytsPerSec);
    return 0;
}

void *map_disk(const char *fname) {
    int fd = open(fname, O_RDWR);

    if (fd < 0) {
        perror(fname);
        goto release;
    }

    off_t size = lseek(fd, 0, SEEK_END);
    if (size == -1) {
        perror(fname);
        goto release;
    }

    struct fat32hdr *hdr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (hdr == (void *)-1) {
        goto release;
    }

    close(fd);

    if (hdr->Signature_word != 0xaa55 ||
            hdr->BPB_TotSec32 * hdr->BPB_BytsPerSec != size) {
        fprintf(stderr, "%s: Not a FAT file image\n", fname);
        goto release;
    }
    return hdr;

release:
    if (fd > 0) {
        close(fd);
    }
    exit(1);
}


int valid_name(const char *name) {
    if (!name || strlen(name) < 5) return 0;
    
    const char *ext = strrchr(name, '.');
    return ext && strncasecmp(ext, ".bmp", 4) == 0;
}




//计算sha1sum，校验和
char *get_sha1sum(void *data, size_t size) {
    // 创建临时文件
    char temp_name[] = "/tmp/bmp_XXXXXX";
    int fd = mkstemp(temp_name);
    if (fd < 0) {
        perror("mkstemp failed");
        return NULL;
    }
    
    // 写入数据
    ssize_t written = write(fd, data, size);
    if (written != (ssize_t)size) {
        perror("write failed");
        close(fd);
        unlink(temp_name);
        return NULL;
    }
    close(fd);
    
    // 计算SHA1
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "sha1sum '%s'", temp_name);  // 引用文件名防止空格问题
    
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        perror("popen failed");
        unlink(temp_name);
        return NULL;
    }
    
    // 正确提取哈希值
    char *sha1 = NULL;
    char line[256];
    if (fgets(line, sizeof(line), fp)) {
        // SHA1值总是40字符
        if (strlen(line) >= 40) {
            sha1 = malloc(41);
            if (sha1) {
                strncpy(sha1, line, 40);
                sha1[40] = '\0';
            }
        }
    }
    
    pclose(fp);
    
    unlink(temp_name);
    return sha1;
}




void recover_func(struct fat32hdr *hdr) {
    uint32_t cluster_size = hdr->BPB_SecPerClus * hdr->BPB_BytsPerSec;
    uint32_t total_clusters = (hdr->BPB_TotSec32 - get_start(hdr)) / hdr->BPB_SecPerClus;
    int entries_per_cluster = cluster_size / sizeof(struct fat32dent);
    
    for (uint32_t cluster = 2; cluster < total_clusters + 2; cluster++) {
        struct fat32dent *entries = (struct fat32dent *)get_data(hdr, cluster);
        
        for (int i = 0; i < entries_per_cluster; i++) {
            struct fat32dent *entry = &entries[i];
            
            if (entry->DIR_Name[0] == 0x00) break;
            if (entry->DIR_Name[0] == 0xE5 && !(entry->DIR_Attr & ATTR_DIRECTORY)) {
                // 处理删除的短文件名条目
                char name[9] = {0}, ext[4] = {0};
                strncpy(name, entry->DIR_Name + 1, 7);
                strncpy(ext, entry->DIR_Name + 8, 3);
                
                // 移除尾部空格
                name[strcspn(name, " ")] = '\0';
                ext[strcspn(ext, " ")] = '\0';
                
                char short_name[13];
                snprintf(short_name, sizeof(short_name), "_%s%s%s", 
                         name, *ext ? "." : "", ext);
                
                if (valid_name(short_name)) {
                    recover_bmp_file(hdr, entry, short_name);
                }
            }
            else if (entry->DIR_Attr == ATTR_LONG_NAME) {
                // 处理长文件名条目
                int lfn_start = i;
                int lfn_count = 0;
                
                // 统计连续的长文件名条目
                while (i < entries_per_cluster && entries[i].DIR_Attr == ATTR_LONG_NAME) {
                    lfn_count++;
                    i++;
                }
                
                if (i < entries_per_cluster) {
                    struct fat32dent *short_entry = &entries[i];
                    
                    if ((short_entry->DIR_Name[0] == 0xE5 || short_entry->DIR_Name[0] != 0x00) &&
                        !(short_entry->DIR_Attr & ATTR_DIRECTORY)) {
                        
                        char *long_name = get_filename(entries, lfn_start, lfn_count);
                        if (long_name && valid_name(long_name)) {
                            recover_bmp_file(hdr, short_entry, long_name);
                        }
                        free(long_name);
                    }
                }
            }
        }
    }
}




char *get_filename(struct fat32dent *entries, int start_idx, int count) {
    char filename[256] = {0};
    int index = 0;
    
    // 从后向前处理长文件名条目
    for (int i = start_idx + count - 1; i >= start_idx; i--) {
        uint8_t *raw = (uint8_t*)&entries[i];
        const int segments[] = {1, 11, 14, 26, 28, 32};
        for (int seg = 0; seg < 3; seg++) {
            for (int j = segments[seg*2]; j < segments[seg*2+1]; j += 2) {
                uint16_t ch = *(uint16_t*)(raw + j);
                if (ch == 0x0000 || ch == 0xFFFF) goto done;
                if (index < sizeof(filename)-1) filename[index++] = ch & 0xFF;
            }
        }
    }
    
done:
    return strdup(filename);
}


bmp_info_t extract_bmp_info(uint8_t *bmp_data) {
    bmp_info_t info = {0};
    
    if (*(uint16_t*)bmp_data != BMP_HEADER_SIGNATURE) 
        return info;

    info.file_size = *(uint32_t*)(bmp_data + 2);
    info.data_offset = *(uint32_t*)(bmp_data + 10);
    
    uint32_t dib_size = *(uint32_t*)(bmp_data + 14);
    if (dib_size >= 40) { 
        info.width = *(uint32_t*)(bmp_data + 18);
        info.height = *(uint32_t*)(bmp_data + 22);
        info.bpp = *(uint16_t*)(bmp_data + 28);
        uint32_t bpp_bytes = info.bpp / 8;
        info.row_bytes = ((info.width * bpp_bytes) + 3) & ~3;
    }
    
    return info;
}



void recover_bmp_file(struct fat32hdr *hdr, struct fat32dent *entry, const char *filename) {
    uint32_t start_cluster = ((uint32_t)entry->DIR_FstClusHI << 16) | entry->DIR_FstClusLO;
    uint32_t file_size = entry->DIR_FileSize;
    uint32_t cluster_size = hdr->BPB_SecPerClus * hdr->BPB_BytsPerSec;
    uint32_t total_clusters = (hdr->BPB_TotSec32 - get_start(hdr)) / hdr->BPB_SecPerClus;
    
    // 基本参数检查
    if (start_cluster < 2 || file_size == 0 || file_size > 100 * 1024 * 1024) 
        return;
    
    // 检查BMP文件头有效性
    uint8_t *start_data = get_data(hdr, start_cluster);
    if (*(uint16_t*)start_data != BMP_HEADER_SIGNATURE) 
        return;
    
    // 提取BMP信息
    bmp_info_t bmp_info = extract_bmp_info(start_data);
    if (!bmp_info.data_offset || !bmp_info.width || !bmp_info.height) 
        return;
    
    // 分配内存
    uint8_t *file_data = malloc(file_size);
    if (!file_data) return;
    
    uint8_t *used_clusters = calloc(total_clusters + 2, 1);
    if (!used_clusters) {
        free(file_data);
        return;
    }

    // 恢复文件数据
    uint32_t bytes_copied = 0;
    uint32_t current_cluster = start_cluster;
    
    while (bytes_copied < file_size && current_cluster >= 2 && current_cluster < total_clusters + 2) {
        used_clusters[current_cluster] = 1;
        uint8_t *cluster_data = get_data(hdr, current_cluster);
        
        // 计算要复制的字节数
        uint32_t to_copy = file_size - bytes_copied;
        if (to_copy > cluster_size) to_copy = cluster_size;
        
        memcpy(file_data + bytes_copied, cluster_data, to_copy);
        bytes_copied += to_copy;
        
        if (bytes_copied >= file_size) break;
        
        // 寻找下一个簇
        current_cluster = find_next_cluster(hdr, cluster_data, &bmp_info, current_cluster,
                                           total_clusters, cluster_size, used_clusters);
        if (!current_cluster) break;
    }
    
    // 验证并输出结果
    if (bytes_copied == file_size && *(uint16_t*)file_data == BMP_HEADER_SIGNATURE) {
        char *checksum = get_sha1sum(file_data, file_size);
        if (checksum) {
            printf("%s %s\n", checksum, filename);
            free(checksum);
        }
    }
    
    free(used_clusters);
    free(file_data);
}

double count_similarity(uint8_t *cur, uint8_t *next, bmp_info_t *info, uint32_t cluster_size) {
    // 头部数据直接视为连续
    if (info->data_offset > cluster_size) 
        return 1.0;
    
    uint32_t bpp = info->bpp / 8;
    if (bpp == 0) return 1.0;
    
    int samples = 0;
    long long diff = 0;
    
    // 只比较最后一行和第一行的边界像素
    uint32_t last_row_start = cluster_size - info->row_bytes;
    if (last_row_start > cluster_size) return 0.0;
    
    for (uint32_t i = 0; i < info->width; i += 4) {  // 每4个像素采样一次
        uint32_t offset = last_row_start + i * bpp;
        if (offset + bpp > cluster_size) break;
        
        for (uint32_t k = 0; k < bpp; k++) {
            diff += abs(cur[offset + k] - next[i * bpp + k]);
        }
        samples++;
    }
    
    return (samples > 0) ? 1.0 - (double)diff / (samples * 255.0 * bpp) : 0.0;
}

uint32_t find_next_cluster(struct fat32hdr *hdr, uint8_t *cur_data, bmp_info_t *info, 
                           uint32_t current_cluster, uint32_t total_clusters, 
                           uint32_t cluster_size, uint8_t *used) {
    // 首先尝试连续簇
    uint32_t next_cluster = current_cluster + 1;
    if (next_cluster < total_clusters + 2 && !used[next_cluster]) {
        double sim = count_similarity(
            cur_data, get_data(hdr, next_cluster), info, cluster_size);
        if (sim >= 0.6) return next_cluster;
    }
    
    // 搜索最相似簇
    uint32_t best_cluster = 0;
    double max_sim = 0.5;  // 最小相似度阈值
    
    for (uint32_t c = 2; c < total_clusters + 2; c++) {
        if (used[c]) continue;
        
        double sim = count_similarity(
            cur_data, get_data(hdr, c), info, cluster_size);
        
        if (sim > max_sim) {
            max_sim = sim;
            best_cluster = c;
        }
    }
    
    return best_cluster;
}


