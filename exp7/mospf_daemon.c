#include "mospf_daemon.h"
#include "mospf_proto.h"
#include "mospf_nbr.h"
#include "mospf_database.h"

#include "rtable.h"
#include "ip.h"
#include <stdbool.h>

#include "list.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>


extern ustack_t *instance;

pthread_mutex_t mospf_lock;


#define MAX_ROUTERS 256  // 最大路由器数量
static int nodes_sorted[MAX_ROUTERS];  // 存储按最短路径长度排序的节点
static int prev[MAX_ROUTERS]; 
static u32 gw_array[MAX_ROUTERS];            // 存储每个路由器对应的网关
static iface_info_t *rt_if_array[MAX_ROUTERS]; // 存储每个路由器对应的出接口
static u32 int_to_rid[MAX_ROUTERS];    // 整数到路由器ID的映射
static struct list_head rid_int_map[MAX_ROUTERS];   // 路由器ID到整数的映射
static int distances[MAX_ROUTERS];      // 最短距离数组
static int graph[MAX_ROUTERS][MAX_ROUTERS]; // 邻接矩阵

void send_mospf_lsu(void);
void mospf_dijkstra(struct list_head *routing_table);
void clear_rtable(void);
void load_rtable(struct list_head *routing_table);
int init_rid_map(struct list_head *rid_int_map);
void init_graph(int num);
void convert_path_to_rtable(int num, struct list_head *routing_table);
static int dest_in_rtable(struct list_head *routing_table, u32 dest, u32 mask);
char *generate_mospf_lsu(int *len);
struct mospf_lsa *copy_lsa_ntoh(struct mospf_lsa *array, int nadv);
void dijkstra(int num, int src);
void add_rid_map(struct list_head *rid_int_map, u32 rid, int *int_id);
int lookup_rid_map(struct list_head *rid_int_map, u32 rid);




void mospf_init()
{
	pthread_mutex_init(&mospf_lock, NULL);

	instance->area_id = 0;
	// get the ip address of the first interface
	iface_info_t *iface = list_entry(instance->iface_list.next, iface_info_t, list);
	instance->router_id = iface->ip;
	instance->sequence_num = 0;
	instance->lsuint = MOSPF_DEFAULT_LSUINT;

	iface = NULL;
	list_for_each_entry(iface, &instance->iface_list, list) {
		iface->helloint = MOSPF_DEFAULT_HELLOINT;
		init_list_head(&iface->nbr_list);
	}

	init_mospf_db();



        // 初始化全局变量
    for (int i = 0; i < MAX_ROUTERS; i++) {
        nodes_sorted[i] = 0;
        prev[i] = -1;
        gw_array[i] = 0;
        rt_if_array[i] = NULL;
        int_to_rid[i] = 0;
        init_list_head(&rid_int_map[i]);
        distances[i] = 0x7fffffff;
        for (int j = 0; j < MAX_ROUTERS; j++) {
            graph[i][j] = 0;
        }
    }
}

void *sending_mospf_hello_thread(void *param);
void *sending_mospf_lsu_thread(void *param);
void *checking_nbr_thread(void *param);
void *checking_database_thread(void *param);

void mospf_run()
{
    //printf("start mospf_run\n");
	pthread_t hello, lsu, nbr, db;
	pthread_create(&hello, NULL, sending_mospf_hello_thread, NULL);
	pthread_create(&lsu, NULL, sending_mospf_lsu_thread, NULL);
	pthread_create(&nbr, NULL, checking_nbr_thread, NULL);
	pthread_create(&db, NULL, checking_database_thread, NULL);
}


///////////////////////////void *sending_mospf_hello_thread(void *param);///////////////////////////
void send_mospf_hello(iface_info_t *iface)
{
    // 1. 计算报文总长度
    int len = ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_HELLO_SIZE;
    char *packet = (char *)malloc(len);
    memset(packet, 0, len);

    // 2. 构造以太网头部（目的MAC为广播地址）
    struct ether_header *eth = (struct ether_header *)packet;
    memset(eth->ether_dhost, 0xff, ETH_ALEN); // 广播
    memcpy(eth->ether_shost, iface->mac, ETH_ALEN);
    eth->ether_type = htons(ETH_P_IP);

    // 3. 构造IP头部
    struct iphdr *ip = (struct iphdr *)(packet + ETHER_HDR_SIZE);
    ip_init_hdr(ip, iface->ip, MOSPF_ALLSPFRouters, IP_BASE_HDR_SIZE + MOSPF_HDR_SIZE + MOSPF_HELLO_SIZE, IPPROTO_MOSPF);

    // 4. 构造MOSPF头部
    struct mospf_hdr *mospf = (struct mospf_hdr *)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE);
    mospf_init_hdr(mospf, MOSPF_TYPE_HELLO, MOSPF_HDR_SIZE + MOSPF_HELLO_SIZE, instance->router_id, instance->area_id);

    // 5. 构造Hello消息
    struct mospf_hello *hello = (struct mospf_hello *)((char *)mospf + MOSPF_HDR_SIZE);
    mospf_init_hello(hello, iface->mask);

    // 6. 计算并设置校验和
    mospf->checksum = mospf_checksum(mospf);

    // 7. 发送数据包
    iface_send_packet(iface, packet, len);

//检查完毕，没问题
}

void *sending_mospf_hello_thread(void *param)
{
    //printf("start sending_mospf_hello_thread\n");
    while (1) {
        sleep(1);
        pthread_mutex_lock(&mospf_lock);

        iface_info_t *iface = NULL;
        list_for_each_entry(iface, &instance->iface_list, list) {
            iface->helloint--;
            if (iface->helloint <= 0) {
                // 发送Hello报文
                send_mospf_hello(iface);
                // 重置hello定时器
                iface->helloint = MOSPF_DEFAULT_HELLOINT;
            }
        }

        pthread_mutex_unlock(&mospf_lock);
    }
    return NULL;
//检查完毕，表面看没问题
}


////////////////////////////void *checking_nbr_thread(void *param)///////////////////

void *checking_nbr_thread(void *param)
{
    //printf("start checking_nbr_thread\n");
    while (1) {
        sleep(1);

        // 创建临时链表存储要删除的邻居
        struct list_head to_delete;
        init_list_head(&to_delete);
        int changed = 0;

        pthread_mutex_lock(&mospf_lock);

        // 遍历所有接口
        iface_info_t *iface;
        list_for_each_entry(iface, &instance->iface_list, list) {
            // 遍历每个接口的所有邻居
            mospf_nbr_t *nbr, *q;
            list_for_each_entry_safe(nbr, q, &iface->nbr_list, list) {
                if (nbr->alive == 0) {
                    log(DEBUG, "neighbor timeout, remove it.\n");
                    // 从接口邻居列表中移除
                    list_delete_entry(&nbr->list);
                    iface->num_nbr--;
                    // 添加到待删除列表
                    list_add_tail(&nbr->list, &to_delete);
                    changed = 1;
                } else {
                    // 递减alive计数器
                    nbr->alive--;
                }
            }
        }

        pthread_mutex_unlock(&mospf_lock);

        // 如果有需要删除的邻居,释放所有待删除的邻居节点,并发送LSU
        if (changed) {
            mospf_nbr_t *nbr, *q;
            list_for_each_entry_safe(nbr, q, &to_delete, list) {
                list_delete_entry(&nbr->list);
                free(nbr);
            }
            instance->sequence_num++;
            send_mospf_lsu();
        }
    }

    return NULL;

//初步检查差不多
}


////////////////////////////////////void *checking_database_thread(void *param)////////////////////




void mospf_update_rtable(void)
{
    // 1. 用于存储Dijkstra算法计算出的路由表项
    struct list_head routing_table;
    init_list_head(&routing_table);

    // 2. 运行Dijkstra算法计算最短路径
    mospf_dijkstra(&routing_table);

    // 3. 清除当前路由表
    clear_rtable();

    // 4. 加载新的路由表
    load_rtable(&routing_table);
    //log(DEBUG, "loaded new routing table.\n");
}



void mospf_dijkstra(struct list_head *routing_table)
{
    //printf("step0\n");
    // 1. 初始化路由器ID到整数的映射
    int n = init_rid_map(rid_int_map);


    // 2. 根据LSDB初始化图结构
    init_graph(n);

    // 3. 运行Dijkstra算法，源节点为0(当前路由器)
    dijkstra(n, 0);

    // 4. 将最短路径转换为路由表项
    convert_path_to_rtable(n, routing_table);
}


int init_rid_map(struct list_head *rid_int_map)
{
    // 记录路由器总数
    int num = 0;

    // 1. 初始化所有链表头
    for (int i = 0; i < MAX_ROUTERS; i++) {
        init_list_head(&rid_int_map[i]);
    }

    //("step1\n");
    // 2. 首先添加本路由器的ID
    add_rid_map(rid_int_map, instance->router_id, &num);
    num++;

    //printf("step2\n");
    // 3. 遍历LSDB
    mospf_db_entry_t *db_entry;
    list_for_each_entry(db_entry, &mospf_db, list) {
        // 添加LSU源路由器的ID
        if (lookup_rid_map(rid_int_map, db_entry->rid) == -1) {
            add_rid_map(rid_int_map, db_entry->rid, &num);
            num++;
        }
        // 添加该路由器所有LSA中的邻居路由器ID
        for (int i = 0; i < db_entry->nadv; i++) {
            u32 nbr_rid = db_entry->array[i].rid;
            if (nbr_rid != 0 && lookup_rid_map(rid_int_map, nbr_rid) == -1) {
                add_rid_map(rid_int_map, nbr_rid, &num);
                num++;
            }
        }
    }

    return num;
//初步检查没问题
}

struct rid_int{
    struct list_head list;
    u32 rid;
    int int_id;
};

u8 hash8(char *addr, int len)
{
    // 简单的哈希函数：每字节异或+加权，最后取低8位
    u8 hash = 0;
    for (int i = 0; i < len; i++) {
        hash = (hash * 131) ^ (u8)addr[i];
    }
    return hash % MAX_ROUTERS; // 保证不会越界
}


void add_rid_map(struct list_head *rid_int_map, u32 rid, int *int_id)
{
    // 计算哈希值作为数组索引
    u8 key = hash8((char *)&rid, 4);
    
    // 创建新节点
    struct rid_int *node = malloc(sizeof(struct rid_int));
    memset(node, 0, sizeof(struct rid_int));
    
    // 设置节点数据
    node->rid = rid;
    node->int_id = *int_id;
    (*int_id)++;
    
    // 将节点添加到对应的链表头
    struct list_head *list = &rid_int_map[key];
    list_add_head(&node->list, list);
}

int lookup_rid_map(struct list_head *rid_int_map, u32 rid)
{
    // 如果rid为0，直接返回0
    if (rid == 0) {
        return 0;
    }

    // 计算rid的哈希值作为数组索引
    u8 key = hash8((char *)&rid, 4);

    // 遍历对应链表中的所有节点
    struct rid_int *node;
    list_for_each_entry(node, &rid_int_map[key], list) {
        if (node->rid == rid) {
            return node->int_id;  // 找到匹配的rid，返回对应的整数索引
        }
    }

    return -1;  // 未找到匹配的rid
}

void init_graph(int num)
{
    // 1. 初始化邻接矩阵为0
    for (int i = 0; i < num; i++) {
        for (int j = 0; j < num; j++) {
            graph[i][j] = 0;
        }
    }

    // 2. 设置当前路由器ID的映射(索引0)
    int_to_rid[0] = instance->router_id;

    // 3. 遍历LSDB构建邻接矩阵
    mospf_db_entry_t *db_entry;
    list_for_each_entry(db_entry, &mospf_db, list) {
        // 获取源路由器的索引
        int src_idx = lookup_rid_map(rid_int_map, db_entry->rid);
        if (src_idx < 0) continue;

        // 记录源路由器ID的映射
        int_to_rid[src_idx] = db_entry->rid;

        // 遍历该路由器的所有邻居
        for (int i = 0; i < db_entry->nadv; i++) {
            u32 nbr_rid = db_entry->array[i].rid;
            if (nbr_rid != 0) {  // 排除无效邻居
                // 获取目标路由器的索引
                int dst_idx = lookup_rid_map(rid_int_map, nbr_rid);
                if (dst_idx < 0) continue;

                // 记录目标路由器ID的映射
                int_to_rid[dst_idx] = nbr_rid;

                // 在邻接矩阵中标记双向连接
                graph[src_idx][dst_idx] = 1;
                graph[dst_idx][src_idx] = 1;
            }
        }
    }
//初步检查没什么问题
}


void convert_path_to_rtable(int num, struct list_head *routing_table)
{
    // 初始化路由表
    init_list_head(routing_table);

    // 先添加直连网络
    iface_info_t *iface;
    list_for_each_entry(iface, &instance->iface_list, list) {
        rt_entry_t *entry = new_rt_entry(
            iface->ip & iface->mask,    // 目的网络
            iface->mask,                // 子网掩码
            0,                          // 直连网络网关为0
            iface                       // 出接口
        );
        list_add_tail(&entry->list, routing_table);
    }

    // 初始化网关和接口数组
    for (int i = 0; i < num; i++) {
        gw_array[i] = 0;
        rt_if_array[i] = NULL;
    }
    // 当前路由器（索引0）不需要设置网关和接口

    // 遍历所有路由器(跳过索引0，即当前路由器)
    for (int i = 1; i < num; i++) {
        int curr = nodes_sorted[i];
        int pre = prev[curr];

        // 如果是直接邻居（前驱为0），查找邻居的IP地址和出接口
        if (pre == 0) {
            // 遍历所有接口和邻居
            iface_info_t *nbr_iface;
            list_for_each_entry(nbr_iface, &instance->iface_list, list) {
                mospf_nbr_t *nbr;
                list_for_each_entry(nbr, &nbr_iface->nbr_list, list) {
                    if (nbr->nbr_id == int_to_rid[curr]) {
                        gw_array[curr] = nbr->nbr_ip;
                        rt_if_array[curr] = nbr_iface;
                        break;
                    }
                }
                if (rt_if_array[curr]) break;
            }
        } else {
            // 不是直接邻居，继承前驱节点的网关和接口
            gw_array[curr] = gw_array[pre];
            rt_if_array[curr] = rt_if_array[pre];
        }

        // 获取当前路由器的LSA信息
        mospf_db_entry_t *db_entry = NULL;
        mospf_db_entry_t *tmp;
        list_for_each_entry(tmp, &mospf_db, list) {
            if (tmp->rid == int_to_rid[curr]) {
                db_entry = tmp;
                break;
            }
        }
        if (!db_entry) continue;

        // 处理该路由器的所有网络
        for (int j = 0; j < db_entry->nadv; j++) {
            u32 dest = db_entry->array[j].network & db_entry->array[j].mask;
            u32 mask = db_entry->array[j].mask;

            // 如果该网络尚未在路由表中且有有效出接口
            if (!dest_in_rtable(routing_table, dest, mask) && rt_if_array[curr]) {
                rt_entry_t *entry = new_rt_entry(
                    dest,                   // 目的网络
                    mask,                   // 子网掩码
                    gw_array[curr],         // 网关
                    rt_if_array[curr]       // 出接口
                );
                list_add_tail(&entry->list, routing_table);
            }
        }
    }
//简单检查完毕，没什么问题
}

void dijkstra(int num, int src)
{
    // 初始化
    int visited[MAX_ROUTERS] = {0};
    for (int i = 0; i < num; i++) {
        distances[i] = (i == src) ? 0 : 0x7fffffff; // 源点距离为0，其余为无穷大
        prev[i] = -1;
        nodes_sorted[i] = i;
    }

    // Dijkstra主循环
    for (int count = 0; count < num; count++) {
        // 选取未访问且距离最小的节点u
        int u = -1, min_dist = 0x7fffffff;
        for (int i = 0; i < num; i++) {
            if (!visited[i] && distances[i] < min_dist) {
                min_dist = distances[i];
                u = i;
            }
        }
        if (u == -1) break; // 剩下的节点不可达

        visited[u] = 1;

        // 更新所有与u相邻的节点的距离
        for (int v = 0; v < num; v++) {
            if (!visited[v] && graph[u][v]) {
                if (distances[u] + 1 < distances[v]) {
                    distances[v] = distances[u] + 1;
                    prev[v] = u;
                }
            }
        }
    }

    // 按距离排序nodes_sorted
    for (int i = 0; i < num - 1; i++) {
        for (int j = i + 1; j < num; j++) {
            if (distances[nodes_sorted[i]] > distances[nodes_sorted[j]]) {
                int tmp = nodes_sorted[i];
                nodes_sorted[i] = nodes_sorted[j];
                nodes_sorted[j] = tmp;
            }
        }
    }
//假设正确
}


// 检查目的网络是否已在路由表中
static int dest_in_rtable(struct list_head *routing_table, u32 dest, u32 mask)
{
    rt_entry_t *entry;
    list_for_each_entry(entry, routing_table, list) {
        if (entry->dest == dest && entry->mask == mask) {
            return 1;
        }
    }
    return 0;
}

void *checking_database_thread(void *param)
{
    //printf("start checking_database_thread\n");
    while (1) {    
	    sleep(1);
        pthread_mutex_lock(&mospf_lock);

        int changed = 0;

        mospf_db_entry_t *db = (mospf_db_entry_t *)mospf_db.next;
        mospf_db_entry_t *q = (mospf_db_entry_t *)(mospf_db.next)->next;
        list_for_each_entry_safe(db, q, &mospf_db, list) {
            db->alive--;
            if (db->alive < 1) {
                // 删除超时的数据库条目
                list_delete_entry(&db->list);
                free(db->array);
                free(db);
                changed = 1;
            }
        }

        if (changed) {
            //可以用print_rtable打印结果
            mospf_update_rtable();
        }

        pthread_mutex_unlock(&mospf_lock);
    }
    return NULL;
//检查没什么问题
}




////////////////////////////////void handle_mospf_hello(iface_info_t *iface, char *packet, int len)/////////////////////

void update_nbr_list_via_hello(iface_info_t *iface, u32 rid, u32 ip, u32 mask, int helloint)
{
    pthread_mutex_lock(&mospf_lock);

    // 1. 先查找是否已存在该邻居,已存在则更新alive时间
    mospf_nbr_t *nbr;
    list_for_each_entry(nbr, &iface->nbr_list, list) {
        if (nbr->nbr_id == rid) {
            nbr->alive = MOSPF_HELLO_TIMEOUT;
            pthread_mutex_unlock(&mospf_lock);
            return;
        }
    }

    // 2. 检查掩码是否匹配
    if (mask != iface->mask) {
        pthread_mutex_unlock(&mospf_lock);
        return;
    }

    // 3. 检查子网是否匹配
    if ((iface->ip & iface->mask) != (ip & mask)) {
        pthread_mutex_unlock(&mospf_lock);
        return;
    }

    // 4. 创建新邻居
    mospf_nbr_t *new_nbr = (mospf_nbr_t *)malloc(sizeof(mospf_nbr_t));
    memset(new_nbr, 0, sizeof(mospf_nbr_t));
    new_nbr->nbr_id = rid;
    new_nbr->nbr_ip = ip;
    new_nbr->nbr_mask = mask;
    new_nbr->alive = MOSPF_HELLO_TIMEOUT;

    // 5. 添加到邻居列表
    list_add_tail(&new_nbr->list, &iface->nbr_list);
    iface->num_nbr++;

    // 6. 更新序列号并发送LSU
    instance->sequence_num++;
    send_mospf_lsu();

    pthread_mutex_unlock(&mospf_lock);
}


void handle_mospf_hello(iface_info_t *iface, const char *packet, int len)
{
	//printf("start handle_mospf_hello\n");
   // 1. 获取ip, mospf, hello报文头
    struct iphdr *ip = packet_to_ip_hdr(packet);
    struct mospf_hdr *mospf = (struct mospf_hdr *)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE);
    struct mospf_hello *hello = (struct mospf_hello *)((char *)mospf + MOSPF_HDR_SIZE);

    // 2. 获取发送方的IP地址
    u32 nbr_ip = ntohl(ip->saddr);

    // 3. 校验mospf检验和
    if (mospf->checksum != mospf_checksum(mospf)) {
        return;
    }

    // 4. 检查子网掩码是否匹配
    if (ntohl(hello->mask) != iface->mask) {
        return;
    }

    // 5. 检查hello间隔是否为MOSPF_DEFAULT_HELLOINT
    if (ntohs(hello->helloint) == MOSPF_DEFAULT_HELLOINT) {
        update_nbr_list_via_hello(
            iface,
            ntohl(mospf->rid),
            nbr_ip,
            ntohl(hello->mask),
            ntohs(hello->helloint)
        );
    }
}


////////////////////////////void *sending_mospf_lsu_thread(void *param)//////////////////////

void send_mospf_lsu(void)
{
    // 1. 生成LSU消息
    int mospf_len;
    char *mospf_msg = generate_mospf_lsu(&mospf_len);
    if (!mospf_msg) {
        // log(ERROR, "Failed to generate LSU message");
        return;
    }

    // 2. 遍历所有接口和邻居，发送LSU
    iface_info_t *iface;
    list_for_each_entry(iface, &instance->iface_list, list) {
        mospf_nbr_t *nbr;
        list_for_each_entry(nbr, &iface->nbr_list, list) {
            // 计算数据包总长度
            int pkt_len = ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + mospf_len;
            char *packet = (char *)malloc(pkt_len);
            memset(packet, 0, pkt_len);

            // 初始化以太网头部
            struct ether_header *eth = (struct ether_header *)packet;
            memset(eth->ether_dhost, 0xff, ETH_ALEN); // 目的MAC为广播
            memcpy(eth->ether_shost, iface->mac, ETH_ALEN);
            eth->ether_type = htons(ETH_P_IP);

            // 复制MOSPF LSU消息到IP负载部分
            memcpy(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE, mospf_msg, mospf_len);

            // 初始化IP头部
            struct iphdr *ip = (struct iphdr *)(packet + ETHER_HDR_SIZE);
            ip_init_hdr(ip, iface->ip, nbr->nbr_ip, IP_BASE_HDR_SIZE + mospf_len, IPPROTO_MOSPF);

            // 发送数据包
            ip_send_packet(packet, pkt_len);

        }
    }

    // 释放LSU消息缓冲区
    free(mospf_msg);
//检查完毕，中间构造头部可能有问题
}


char *generate_mospf_lsu(int *len)
{
    // 1. 计算所有邻居数量（没有邻居的接口也算一个LSA）
    int nadv = 0;
    iface_info_t *iface;
    list_for_each_entry(iface, &instance->iface_list, list) {
        if (iface->num_nbr == 0) {
            nadv += 1;
        } else {
            nadv += iface->num_nbr;
        }
    }

    // 2. 计算消息总长度
    *len = MOSPF_HDR_SIZE + MOSPF_LSU_SIZE + nadv * MOSPF_LSA_SIZE;

    // 3. 分配内存
    char *buffer = malloc(*len);
    memset(buffer, 0, *len);

    struct mospf_hdr *mospf = (struct mospf_hdr *)buffer;
    struct mospf_lsu *lsu = (struct mospf_lsu *)(buffer + MOSPF_HDR_SIZE);
    struct mospf_lsa *lsa = (struct mospf_lsa *)(buffer + MOSPF_HDR_SIZE + MOSPF_LSU_SIZE);

    // 4. 初始化MOSPF头部
    mospf_init_hdr(mospf, MOSPF_TYPE_LSU, *len, instance->router_id, instance->area_id);

    // 5. 初始化LSU头部
    mospf_init_lsu(lsu, nadv);

    // 6. 填充LSA条目
    int idx = 0;
    list_for_each_entry(iface, &instance->iface_list, list) {
        if (iface->num_nbr == 0) {
            // 没有邻居的接口，添加一个空条目
            lsa[idx].network = htonl(iface->ip & iface->mask);
            lsa[idx].mask = htonl(iface->mask);
            lsa[idx].rid = 0;
            idx++;
        } else {
            mospf_nbr_t *nbr;
            list_for_each_entry(nbr, &iface->nbr_list, list) {
                lsa[idx].network = htonl(nbr->nbr_ip & nbr->nbr_mask);
                lsa[idx].mask = htonl(nbr->nbr_mask);
                lsa[idx].rid = htonl(nbr->nbr_id);
                idx++;
            }
        }
    }

    // 7. 计算校验和
    mospf->checksum = mospf_checksum(mospf);

    return buffer;

//初步检查应该没问题
}

void *sending_mospf_lsu_thread(void *param)
{
    //printf("start sending_mospf_lsu_thread\n");
	while (1) {
        sleep(1);
        pthread_mutex_lock(&mospf_lock);

        instance->lsuint--;
        if (instance->lsuint <= 0) {
            // 发送LSU报文
            send_mospf_lsu();
            // 更新序列号
            instance->sequence_num++;
            // 重置计时器
            instance->lsuint = MOSPF_DEFAULT_LSUINT;
        }

        pthread_mutex_unlock(&mospf_lock);
    }

	return NULL;
}

////////////////////////////////void handle_mospf_lsu(iface_info_t *iface, char *packet, int len)//////////////////


// 遍历MOSPF数据库中的所有条目,如果找到匹配的router ID，返回该条目;未找到匹配条目，返回NULL
mospf_db_entry_t *find_mospf_db_entry(iface_info_t *iface, u32 rid)
{
    mospf_db_entry_t *db_entry;
    list_for_each_entry(db_entry, &mospf_db, list) {
        if (db_entry->rid == rid) {
            return db_entry;
        }
    }
    return NULL;
}

// 如果是新条目，db_entry创建新条目;否则更新现有条目
void update_mospf_db_via_lsu(mospf_db_entry_t *db_entry, iface_info_t *iface, 
    u32 rid, struct mospf_lsu *lsu)
{
    u16 seq = ntohs(lsu->seq);
    u32 nadv = ntohl(lsu->nadv);

    if (!db_entry) {
        // 创建新条目
        db_entry = (mospf_db_entry_t *)malloc(sizeof(mospf_db_entry_t));
        memset(db_entry, 0, sizeof(mospf_db_entry_t));
        db_entry->rid = rid;
        db_entry->seq = seq;
        db_entry->nadv = nadv;
        db_entry->alive = MOSPF_DATABASE_TIMEOUT;
        db_entry->array = copy_lsa_ntoh((struct mospf_lsa *)(lsu + 1), nadv);
        list_add_tail(&db_entry->list, &mospf_db);
    } else {
        // 更新现有条目
        if (seq > db_entry->seq) {
            db_entry->seq = seq;
            db_entry->nadv = nadv;
            db_entry->alive = MOSPF_DATABASE_TIMEOUT;
            if (db_entry->array) free(db_entry->array);
            db_entry->array = copy_lsa_ntoh((struct mospf_lsa *)(lsu + 1), nadv);
        } else {
            // 只更新时间
            db_entry->alive = MOSPF_DATABASE_TIMEOUT;
        }
    }

    //可以用print_rtable打印结果
}

// 复制并转换LSA数组（网络字节序到主机字节序）
struct mospf_lsa *copy_lsa_ntoh(struct mospf_lsa *array, int nadv)
{
    struct mospf_lsa *new_array = malloc(nadv * sizeof(struct mospf_lsa));
    for (int i = 0; i < nadv; i++) {
        new_array[i].network = ntohl(array[i].network);
        new_array[i].mask = ntohl(array[i].mask);
        new_array[i].rid = ntohl(array[i].rid);
    }
    return new_array;
}


void forward_mospf_lsu(iface_info_t *iface, char *packet, int len)
{
    // 获取IP头部
    struct iphdr *ip = packet_to_ip_hdr(packet);
    // 获取MOSPF头部
    struct mospf_hdr *mospf = (struct mospf_hdr *)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE);
    struct mospf_lsu *lsu = (struct mospf_lsu *)((char *)mospf + MOSPF_HDR_SIZE);

    // 检查TTL
    if (lsu->ttl <= 0) {
        return;
    }

    // 遍历所有接口
    iface_info_t *tx_iface;
    list_for_each_entry(tx_iface, &instance->iface_list, list) {
        // 跳过接收接口
        if (tx_iface == iface)
            continue;

        // 遍历该接口的所有邻居
        mospf_nbr_t *nbr;
        list_for_each_entry(nbr, &tx_iface->nbr_list, list) {
            // 为每个邻居创建一个新的数据包副本
            char *new_packet = (char *)malloc(len);
            memcpy(new_packet, packet, len);

            // 更新以太网头部(广播地址)
            struct ether_header *eth = (struct ether_header *)new_packet;
            memset(eth->ether_dhost, 0xff, ETH_ALEN);
            memcpy(eth->ether_shost, tx_iface->mac, ETH_ALEN);
            eth->ether_type = htons(ETH_P_IP);

            // 更新IP头部
            struct iphdr *new_ip = (struct iphdr *)(new_packet + ETHER_HDR_SIZE);
            ip_init_hdr(new_ip, tx_iface->ip, nbr->nbr_ip, len - ETHER_HDR_SIZE, IPPROTO_MOSPF);

            // 发送数据包
            ip_send_packet(new_packet, len);
        }
    }
}


void handle_mospf_lsu(iface_info_t *iface, char *packet, int len)
{
	//printf("start handle_mospf_lsu\n");
    // 1. 获取LSU消息头
    struct iphdr *ip = packet_to_ip_hdr(packet);
    struct mospf_hdr *mospf = (struct mospf_hdr *)(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE);
    struct mospf_lsu *lsu = (struct mospf_lsu *)((char *)mospf + MOSPF_HDR_SIZE);

    // 2. 校验mospf检验和
    if (mospf->checksum != mospf_checksum(mospf)) {
        // 校验和错误，丢弃
        return;
    }

    // 3. 获取发送方的router id
    u32 rid = ntohl(mospf->rid);

    // 4. 如果LSU来自自己，直接忽略
    if (rid == instance->router_id)
        return;

    // 5. 获取序列号
    u16 seq = ntohs(lsu->seq);



    // 6. 在数据库中查找此路由器的条目
    mospf_db_entry_t *db_entry = find_mospf_db_entry(iface, rid);

    // 7. 更新数据库
    update_mospf_db_via_lsu(db_entry, iface, rid, lsu);

  

    // 8. 减少TTL并转发
    lsu->ttl--;
    mospf->checksum = mospf_checksum(mospf);

    if (lsu->ttl > 0) {
        forward_mospf_lsu(iface, packet, len);
    }

    //printf("step\n");
    // 9. 更新路由表
    mospf_update_rtable();
    //printf("end handle_mospf_lsu\n");

}

void handle_mospf_packet(iface_info_t *iface, char *packet, int len)
{
	struct iphdr *ip = (struct iphdr *)(packet + ETHER_HDR_SIZE);
	struct mospf_hdr *mospf = (struct mospf_hdr *)((char *)ip + IP_HDR_SIZE(ip));

	if (mospf->version != MOSPF_VERSION) {
		log(ERROR, "received mospf packet with incorrect version (%d)", mospf->version);
		return ;
	}
	if (mospf->checksum != mospf_checksum(mospf)) {
		log(ERROR, "received mospf packet with incorrect checksum");
		return ;
	}
	if (ntohl(mospf->aid) != instance->area_id) {
		log(ERROR, "received mospf packet with incorrect area id");
		return ;
	}

	switch (mospf->type) {
		case MOSPF_TYPE_HELLO:
			handle_mospf_hello(iface, packet, len);
			break;
		case MOSPF_TYPE_LSU:
			handle_mospf_lsu(iface, packet, len);
			break;
		default:
			log(ERROR, "received mospf packet with unknown type (%d).", mospf->type);
			break;
	}
}
