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

// 簇类型分类
typedef enum {
    CLUSTER_DIRECTORY,
    CLUSTER_BMP_HEADER,
    CLUSTER_BMP_DATA,
    CLUSTER_UNUSED
} cluster_type_t;

// BMP文件信息结构
typedef struct {
    uint32_t data_offset;    // bitmap数据开始的偏移
    uint32_t width;          // 图片宽度
    uint32_t height;         // 图片高度
    uint32_t file_size;      // 文件总大小
    uint16_t bits_per_pixel; // 每像素位数
} bmp_info_t;

void *map_disk(const char *fname);
void recover_filenames(struct fat32hdr *hdr);
uint8_t *get_cluster_data(struct fat32hdr *hdr, uint32_t cluster);
uint32_t get_data_start_sector(struct fat32hdr *hdr);
void scan_for_deleted_files(struct fat32hdr *hdr);
int is_long_filename_entry(struct fat32dent *entry);
char *extract_long_filename(struct fat32dent *entries, int start_idx, int count);
void print_filename(struct fat32dent *entry);
int is_valid_bmp_name(const char *name);

// 簇分类函数
cluster_type_t classify_cluster(uint8_t *cluster_data, uint32_t cluster_size);
int is_bmp_header_cluster(uint8_t *cluster_data);
int is_directory_cluster(uint8_t *cluster_data, uint32_t cluster_size);
int is_bmp_data_cluster(uint8_t *cluster_data, uint32_t cluster_size);

// BMP文件处理函数
bmp_info_t extract_bmp_info(uint8_t *bmp_data);
void recover_bmp_file(struct fat32hdr *hdr, struct fat32dent *entry, const char *filename);
char *calculate_sha1sum(uint8_t *data, uint32_t size);

// 新增函数声明
double calculate_pixel_similarity(uint8_t *current_data, uint8_t *next_data, 
                                 uint32_t bytes_copied, bmp_info_t *bmp_info, 
                                 uint32_t cluster_size);
uint32_t find_most_similar_cluster(struct fat32hdr *hdr, uint8_t *current_data,
                                   uint32_t bytes_copied, bmp_info_t *bmp_info,
                                   uint32_t current_cluster, uint32_t total_clusters, 
                                   uint32_t cluster_size, uint8_t *used_clusters);

// 长文件名常量
#define ATTR_LONG_NAME 0x0F
#define LFN_LAST_ENTRY 0x40

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s fs-image\n", argv[0]);
        exit(1);
    }

    setbuf(stdout, NULL);

    assert(sizeof(struct fat32hdr) == 512); // defensive

    // map disk image to memory
    struct fat32hdr *hdr = map_disk(argv[1]);

    // 恢复文件名
    recover_filenames(hdr);

    // file system traversal
    munmap(hdr, hdr->BPB_TotSec32 * hdr->BPB_BytsPerSec);
    return 0;
}

void recover_filenames(struct fat32hdr *hdr) {
    // 扫描寻找删除的文件
    scan_for_deleted_files(hdr);
}

uint32_t get_data_start_sector(struct fat32hdr *hdr) {
    return hdr->BPB_RsvdSecCnt + (hdr->BPB_NumFATs * hdr->BPB_FATSz32);
}

uint8_t *get_cluster_data(struct fat32hdr *hdr, uint32_t cluster) {
    uint32_t data_start_sector = get_data_start_sector(hdr);
    uint32_t sector = data_start_sector + (cluster - 2) * hdr->BPB_SecPerClus;
    return (uint8_t*)hdr + sector * hdr->BPB_BytsPerSec;
}

void scan_for_deleted_files(struct fat32hdr *hdr) {
    uint32_t cluster_size = hdr->BPB_SecPerClus * hdr->BPB_BytsPerSec;
    uint32_t data_start_sector = get_data_start_sector(hdr);
    uint32_t total_clusters = (hdr->BPB_TotSec32 - data_start_sector) / hdr->BPB_SecPerClus;
    
    int file_count = 0;
    
    // 扫描整个数据区，只处理目录簇
    for (uint32_t cluster = 2; cluster < total_clusters + 2; cluster++) {
        uint8_t *cluster_data = get_cluster_data(hdr, cluster);
        
        // 分类簇类型
       // cluster_type_t type = classify_cluster(cluster_data, cluster_size);
        
        // 只处理目录簇
        if (!is_directory_cluster) {
            continue;
        }
        
        struct fat32dent *entries = (struct fat32dent *)cluster_data;
        
        // 检查这个簇中的每个32字节块，看是否是目录项
        for (int i = 0; i < cluster_size / 32; i++) {
            struct fat32dent *entry = &entries[i];
            
            // 跳过空白区域
            if (entry->DIR_Name[0] == 0x00) break; // 目录中遇到空条目后应该结束
            
            // 检查是否是长文件名目录项
            if (is_long_filename_entry(entry)) {
                // 寻找完整的长文件名序列
                int lfn_start = i;
                int lfn_count = 0;
                
                // 计算长文件名目录项数量 - LFN条目以倒序存储
                int temp_i = i;
                while (temp_i < cluster_size / 32 && is_long_filename_entry(&entries[temp_i])) {
                    lfn_count++;
                    temp_i++;
                }
                
                // 查找对应的8.3目录项
                if (temp_i < cluster_size / 32) {
                    struct fat32dent *short_entry = &entries[temp_i];
                    
                    // 检查是否是有效的8.3目录项（包括删除的）且不是目录
                    if ((short_entry->DIR_Name[0] == 0xE5 || 
                        (short_entry->DIR_Name[0] != 0x00 && 
                         short_entry->DIR_Attr != ATTR_LONG_NAME)) &&
                        !(short_entry->DIR_Attr & ATTR_DIRECTORY)) {
                        
                        char *long_name = extract_long_filename(entries, lfn_start, lfn_count);
                        if (long_name && is_valid_bmp_name(long_name)) {
                            // 尝试恢复完整的BMP文件
                            recover_bmp_file(hdr, short_entry, long_name);
                            file_count++;
                        }
                        if (long_name) free(long_name);
                    }
                }
                
                // 跳过所有LFN条目和8.3条目
                i = temp_i;
            }
            // 检查单独的删除文件（没有长文件名）
            else if (entry->DIR_Name[0] == 0xE5 && 
                     entry->DIR_Attr != ATTR_LONG_NAME &&
                     entry->DIR_Attr != 0x00 &&
                     !(entry->DIR_Attr & ATTR_DIRECTORY)) { // 排除目录
                
                // 重建短文件名来检查是否是BMP文件
                char short_name[13];
                char name[9] = {0}, ext[4] = {0};
                int name_len = 0;
                for(int k=1; k<8; ++k) {
                    if(entry->DIR_Name[k] != ' ') name[name_len++] = entry->DIR_Name[k];
                }
                int ext_len = 0;
                for(int k=8; k<11; ++k) {
                    if(entry->DIR_Name[k] != ' ') ext[ext_len++] = entry->DIR_Name[k];
                }
                
                if (ext_len > 0) {
                    snprintf(short_name, sizeof(short_name), "?%s.%s", name, ext);
                } else {
                    snprintf(short_name, sizeof(short_name), "?%s", name);
                }
                
                if (is_valid_bmp_name(short_name)) {
                    // 尝试恢复完整的BMP文件
                    recover_bmp_file(hdr, entry, short_name);
                    file_count++;
                }
            }
        }
    }
}

int is_long_filename_entry(struct fat32dent *entry) {
    return entry->DIR_Attr == ATTR_LONG_NAME;
}

char *extract_long_filename(struct fat32dent *entries, int start_idx, int count) {
    if (count <= 0) return NULL;
    
    // 分配足够的空间存储文件名
    char *filename = malloc(256);
    if (!filename) return NULL;
    
    // 创建一个映射数组来按序列号排序LFN条目
    struct fat32dent *sorted_entries[count];
    memset(sorted_entries, 0, sizeof(sorted_entries));
    
    // 首先找到所有LFN条目并按序列号排序
    // 注意：LFN条目在磁盘上是倒序存储的
    for (int i = 0; i < count; i++) {
        struct fat32dent *entry = &entries[start_idx + i];
        uint8_t seq_byte = entry->DIR_Name[0];
        uint8_t seq_num = seq_byte & 0x3F; // 移除LFN_LAST_ENTRY标志
        
        // 验证序列号有效性（序列号从1开始）
        if (seq_num > 0 && seq_num <= count) {
            sorted_entries[seq_num - 1] = entry; // 序列号从1开始，数组从0开始
        }
    }
    
    // 检查是否有缺失的序列号
    for (int i = 0; i < count; i++) {
        if (sorted_entries[i] == NULL) {
            free(filename);
            return NULL;
        }
    }
    
    int pos = 0;
    
    // 按正确顺序提取字符（序列号1, 2, 3...）
    for (int i = 0; i < count; i++) {
        struct fat32dent *entry = sorted_entries[i];
        uint8_t *raw = (uint8_t*)entry;
        
        // 提取第1-5个字符 (偏移1-10)
        for (int j = 1; j < 11; j += 2) {
            uint16_t ch = *(uint16_t*)(raw + j);
            if (ch == 0x0000 || ch == 0xFFFF) goto done;
            if (pos < 255) filename[pos++] = (char)(ch & 0xFF);
        }
        
        // 提取第6-11个字符 (偏移14-25)
        for (int j = 14; j < 26; j += 2) {
            uint16_t ch = *(uint16_t*)(raw + j);
            if (ch == 0x0000 || ch == 0xFFFF) goto done;
            if (pos < 255) filename[pos++] = (char)(ch & 0xFF);
        }
        
        // 提取第12-13个字符 (偏移28-31)
        for (int j = 28; j < 32; j += 2) {
            uint16_t ch = *(uint16_t*)(raw + j);
            if (ch == 0x0000 || ch == 0xFFFF) goto done;
            if (pos < 255) filename[pos++] = (char)(ch & 0xFF);
        }
    }
    
done:
    filename[pos] = '\0';
    return filename;
}

void print_filename(struct fat32dent *entry) {
    // 打印主文件名
    for (int i = 0; i < 8; i++) {
        if (entry->DIR_Name[i] == ' ') break;
        printf("%c", entry->DIR_Name[i]);
    }
    
    // 检查是否有扩展名
    if (entry->DIR_Name[8] != ' ') {
        printf(".");
        for (int i = 8; i < 11; i++) {
            if (entry->DIR_Name[i] == ' ') break;
            printf("%c", entry->DIR_Name[i]);
        }
    }
}

int is_valid_bmp_name(const char *name) {
    if (!name) return 0;
    
    int len = strlen(name);
    if (len < 5) return 0;  // 至少要有 "x.bmp"
    
    // 检查扩展名
    const char *ext = strrchr(name, '.');
    if (!ext) return 0;
    
    return (strcasecmp(ext, ".bmp") == 0);
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

// 簇分类函数
cluster_type_t classify_cluster(uint8_t *cluster_data, uint32_t cluster_size) {
    // 1. 检查是否是BMP头部 - "BM" (0x424D)
    if (is_bmp_header_cluster(cluster_data)) {
        return CLUSTER_BMP_HEADER;
    }
    
    // 2. 检查是否是目录簇
    if (is_directory_cluster(cluster_data, cluster_size)) {
        return CLUSTER_DIRECTORY;
    }
    
    // 3. 检查是否是BMP数据
    if (is_bmp_data_cluster(cluster_data, cluster_size)) {
        return CLUSTER_BMP_DATA;
    }
    
    return CLUSTER_UNUSED;
}

int is_bmp_header_cluster(uint8_t *cluster_data) {
    // BMP文件头特征：以"BM"开头
    if (cluster_data[0] == 0x42 && cluster_data[1] == 0x4D) { // "BM"
        // 检查文件大小字段是否合理
        uint32_t file_size = *(uint32_t*)(cluster_data + 2);
        if (file_size > 54 && file_size < 100*1024*1024) { // 54字节到100MB
            // 检查数据偏移是否合理
            uint32_t data_offset = *(uint32_t*)(cluster_data + 10);
            if (data_offset >= 54 && data_offset < 4096) {
                return 1;
            }
        }
    }
    return 0;
}

int is_directory_cluster(uint8_t *cluster_data, uint32_t cluster_size) {
    struct fat32dent *entries = (struct fat32dent *)cluster_data;
    int valid_entries = 0;
    int bmp_count = 0;
    int total_checked = 0;
    
    // 检查前32个目录项或直到遇到空条目
    for (int i = 0; i < cluster_size / 32 && i < 32; i++) {
        struct fat32dent *entry = &entries[i];
        total_checked++;
        
        // 遇到空条目，目录应该结束
        if (entry->DIR_Name[0] == 0x00) {
            break;
        }
        
        // 检查是否像有效的目录项
        uint8_t attr = entry->DIR_Attr;
        uint8_t first_byte = entry->DIR_Name[0];
        
        if (attr == ATTR_LONG_NAME ||                    // 长文件名
            attr == ATTR_DIRECTORY ||                    // 目录
            attr == ATTR_ARCHIVE ||                      // 文件
            (attr & ATTR_ARCHIVE) ||                     // 文件（可能有其他属性）
            (first_byte == 0xE5 && attr != 0x00)) {     // 删除的文件
            
            valid_entries++;
            
            // 计算BMP扩展名出现次数 - 目录文件的重要特征
            if (attr != ATTR_LONG_NAME) {
                // 检查扩展名是否为BMP
                if (entry->DIR_Name[8] == 'B' && 
                    entry->DIR_Name[9] == 'M' && 
                    entry->DIR_Name[10] == 'P') {
                    bmp_count++;
                }
            }
            
            // 检查文件名字符的合理性
            int printable_chars = 0;
            for (int j = 0; j < 11; j++) {
                uint8_t ch = entry->DIR_Name[j];
                if (j == 0 && ch == 0xE5) continue; // 删除标记
                if ((ch >= 0x20 && ch <= 0x7E) || ch == 0x20) {
                    printable_chars++;
                }
            }
            
            if (printable_chars < 6) {
                valid_entries--; // 文件名不合理，不算有效条目
            }
        }
        
        // 如果前几个条目都无效，可能不是目录
        if (total_checked >= 8 && valid_entries == 0) {
            return 0;
        }
    }
    
    // 目录簇的判断标准：
    // 1. 有合理比例的有效目录项
    // 2. 包含BMP扩展名（题目说目录总是包含大量BMP字符）
    return total_checked > 0 && valid_entries > 0 && 
           (bmp_count > 0 || (valid_entries * 100 / total_checked) >= 70);
}

int is_bmp_data_cluster(uint8_t *cluster_data, uint32_t cluster_size) {
    // BMP像素数据的特征：相邻像素颜色变化通常较小
    int smooth_transitions = 0;
    int total_transitions = 0;
    
    // 按3字节一组检查像素数据（RGB）
    for (int i = 3; i < cluster_size - 3 && i < 1024; i += 3) {
        uint8_t r1 = cluster_data[i-3], g1 = cluster_data[i-2], b1 = cluster_data[i-1];
        uint8_t r2 = cluster_data[i], g2 = cluster_data[i+1], b2 = cluster_data[i+2];
        
        // 计算相邻像素的颜色差异
        int r_diff = abs(r1 - r2);
        int g_diff = abs(g1 - g2);
        int b_diff = abs(b1 - b2);
        int total_diff = r_diff + g_diff + b_diff;
        
        total_transitions++;
        
        // 如果颜色变化较小，认为是平滑变化
        if (total_diff < 80) {
            smooth_transitions++;
        }
    }
    
    // 如果大部分变化都很平滑，可能是图像数据
    return total_transitions > 100 && 
           (smooth_transitions * 100 / total_transitions) > 60;
}

// BMP信息提取函数
bmp_info_t extract_bmp_info(uint8_t *bmp_data) {
    bmp_info_t info = {0};
    
    // 验证BMP签名
    if (bmp_data[0] != 0x42 || bmp_data[1] != 0x4D) {
        return info;
    }
    
    // 读取BITMAPFILEHEADER
    info.file_size = *(uint32_t*)(bmp_data + 2);
    info.data_offset = *(uint32_t*)(bmp_data + 10);
    
    // 读取BITMAPINFOHEADER
    uint32_t dib_size = *(uint32_t*)(bmp_data + 14);
    if (dib_size >= 40) { // 标准的BITMAPINFOHEADER
        info.width = *(uint32_t*)(bmp_data + 18);
        info.height = *(uint32_t*)(bmp_data + 22);
        info.bits_per_pixel = *(uint16_t*)(bmp_data + 28);
    }
    
    return info;
}

// BMP文件恢复函数
void recover_bmp_file(struct fat32hdr *hdr, struct fat32dent *entry, const char *filename) {
    // 获取文件基本信息
    uint32_t start_cluster = ((uint32_t)entry->DIR_FstClusHI << 16) | entry->DIR_FstClusLO;
    uint32_t file_size = entry->DIR_FileSize;
    uint32_t cluster_size = hdr->BPB_SecPerClus * hdr->BPB_BytsPerSec;
    
    if (start_cluster < 2 || file_size == 0) {
        printf("0000000000000000000000000000000000000000 %s\n", filename);
        return;
    }
    
    // 获取起始簇数据
    uint8_t *start_cluster_data = get_cluster_data(hdr, start_cluster);
    
    // 验证这确实是BMP头部
    if (!is_bmp_header_cluster(start_cluster_data)) {
        printf("0000000000000000000000000000000000000000 %s\n", filename);
        return;
    }
    
    // 提取BMP信息
    bmp_info_t bmp_info = extract_bmp_info(start_cluster_data);
    
    if (bmp_info.data_offset == 0 || bmp_info.width == 0 || bmp_info.height == 0) {
        printf("0000000000000000000000000000000000000000 %s\n", filename);
        return;
    }
    
    // 分配文件数据缓冲区
    uint8_t *file_data = malloc(file_size);
    if (!file_data) {
        printf("0000000000000000000000000000000000000000 %s\n", filename);
        return;
    }
    
    uint32_t data_start_sector = get_data_start_sector(hdr);
    uint32_t total_clusters = (hdr->BPB_TotSec32 - data_start_sector) / hdr->BPB_SecPerClus;
    
    // 创建一个位图来跟踪已使用的簇
    uint8_t *used_clusters = calloc(total_clusters + 2, sizeof(uint8_t));
    if (!used_clusters) {
        free(file_data);
        printf("0000000000000000000000000000000000000000 %s\n", filename);
        return;
    }

    // 恢复文件数据
    uint32_t bytes_copied = 0;
    uint32_t current_cluster = start_cluster;
    const double SIMILARITY_THRESHOLD = 0.6; // 相似度阈值

    while (bytes_copied < file_size && current_cluster >= 2 && current_cluster < total_clusters + 2) {
        used_clusters[current_cluster] = 1; // 标记为已使用
        uint8_t *cluster_data = get_cluster_data(hdr, current_cluster);
        uint32_t bytes_to_copy = (file_size - bytes_copied > cluster_size) ? 
                                cluster_size : (file_size - bytes_copied);
        
        memcpy(file_data + bytes_copied, cluster_data, bytes_to_copy);
        bytes_copied += bytes_to_copy;
        
        if (bytes_copied >= file_size) break;
        
        // 优先使用连续策略预测下一个簇
        uint32_t next_cluster_candidate = current_cluster + 1;
        
        if (next_cluster_candidate < total_clusters + 2 && !used_clusters[next_cluster_candidate]) {
            uint8_t *next_cluster_data = get_cluster_data(hdr, next_cluster_candidate);
            double similarity = calculate_pixel_similarity(cluster_data, next_cluster_data,
                                                           bytes_copied, &bmp_info, cluster_size);
            
            // 如果连续策略的相似度足够高，则采用
            if (similarity >= SIMILARITY_THRESHOLD) {
                current_cluster = next_cluster_candidate;
                continue;
            }
        }
        
        // 否则，在所有未使用的簇中寻找最相似的
        uint32_t best_match = find_most_similar_cluster(hdr, cluster_data, bytes_copied, &bmp_info,
                                                        current_cluster, total_clusters, cluster_size, used_clusters);
        
        if (best_match != 0) {
            current_cluster = best_match;
        } else {
            // 如果找不到相似的，退回到连续策略（即使相似度低）
            current_cluster++; 
        }
    }
    
    free(used_clusters);

    // 验证文件完整性
    if (bytes_copied == file_size && 
        file_data[0] == 0x42 && file_data[1] == 0x4D) {
        
        // 计算SHA1校验和
        char *checksum = calculate_sha1sum(file_data, file_size);
        if (checksum) {
            printf("%s %s\n", checksum, filename);
            free(checksum);
        } else {
            printf("0000000000000000000000000000000000000000 %s\n", filename);
        }
    } else {
        // 文件恢复失败，输出默认校验和
        printf("0000000000000000000000000000000000000000 %s\n", filename);
    }
    
    free(file_data);
}

// 计算两个相邻簇之间的像素相似度
double calculate_pixel_similarity(uint8_t *current_data, uint8_t *next_data, 
                                 uint32_t bytes_copied, bmp_info_t *bmp_info, 
                                 uint32_t cluster_size) {
    if (bytes_copied < bmp_info->data_offset) {
        return 1.0; // 头部数据假设是连续的
    }

    uint32_t bytes_per_pixel = bmp_info->bits_per_pixel / 8;
    if (bytes_per_pixel == 0) return 1.0; // 对于非标准像素格式，不进行比较
    uint32_t row_bytes = ((bmp_info->width * bytes_per_pixel) + 3) & ~3;

    int total_samples = 0;
    long long total_diff = 0;

    // 比较左边界：next_data的第一列 vs current_data的最后一列
    for (uint32_t i = 0; i < cluster_size; i += row_bytes) {
        if (i + bytes_per_pixel > cluster_size) break;
        uint8_t *p1 = current_data + cluster_size - bytes_per_pixel;
        uint8_t *p2 = next_data;
        for(uint32_t k=0; k<bytes_per_pixel; ++k) total_diff += abs((int)p1[k] - (int)p2[k]);
        total_samples++;
    }

    // 比较上边界：next_data的第一行 vs current_data的最后一行
    for (uint32_t i = 0; i < cluster_size; i += bytes_per_pixel * 4) { // 抽样比较
        if (i + bytes_per_pixel > cluster_size) break;
        uint8_t *p1 = current_data + cluster_size - row_bytes + i;
        uint8_t *p2 = next_data + i;
        if ((uintptr_t)p1 < (uintptr_t)current_data || (uintptr_t)p1 >= (uintptr_t)(current_data + cluster_size)) continue;
        for(uint32_t k=0; k<bytes_per_pixel; ++k) total_diff += abs((int)p1[k] - (int)p2[k]);
        total_samples++;
    }

    if (total_samples == 0) return 0.0;
    double avg_diff = (double)total_diff / total_samples;
    
    // 差异越小，相似度越高。将差异转换为0-1的相似度
    return 1.0 - (avg_diff / (255.0 * bytes_per_pixel));
}

// 查找最相似的簇
uint32_t find_most_similar_cluster(struct fat32hdr *hdr, uint8_t *current_data,
                                   uint32_t bytes_copied, bmp_info_t *bmp_info,
                                   uint32_t current_cluster, uint32_t total_clusters, 
                                   uint32_t cluster_size, uint8_t *used_clusters) {
    uint32_t best_cluster = 0;
    double max_similarity = -1.0;

    // 搜索所有未使用的簇
    for (uint32_t c = 2; c < total_clusters + 2; c++) {
        if (used_clusters[c]) continue;

        uint8_t *candidate_data = get_cluster_data(hdr, c);
        double similarity = calculate_pixel_similarity(current_data, candidate_data, bytes_copied, bmp_info, cluster_size);

        if (similarity > max_similarity) {
            max_similarity = similarity;
            best_cluster = c;
        }
    }
    
    // 只在找到一个足够好的匹配时才返回
    if (max_similarity > 0.5) {
        return best_cluster;
    }

    return 0; // 未找到合适的簇
}

// SHA1校验和计算函数
char *calculate_sha1sum(uint8_t *data, uint32_t size) {
    // 创建临时文件
    char temp_filename[] = "/tmp/bmp_recovery_XXXXXX";
    int fd = mkstemp(temp_filename);
    if (fd < 0) return NULL;
    
    // 写入数据
    if (write(fd, data, size) != (ssize_t)size) {
        close(fd);
        unlink(temp_filename);
        return NULL;
    }
    close(fd);
    
    // 调用sha1sum计算校验和
    char command[256];
    snprintf(command, sizeof(command), "sha1sum %s", temp_filename);
    
    FILE *fp = popen(command, "r");
    if (!fp) {
        unlink(temp_filename);
        return NULL;
    }
    
    char *checksum = malloc(41); // SHA1是40个字符 + '\0'
    if (checksum && fscanf(fp, "%40s", checksum) == 1) {
        pclose(fp);
        unlink(temp_filename);
        return checksum;
    }
    
    pclose(fp);
    unlink(temp_filename);
    if (checksum) free(checksum);
    return NULL;
}