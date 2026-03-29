#include "tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

treeNode* root = NULL; //全局变量，前缀树的根节点
treeNode_advance* root_advance = NULL; //全局变量，前缀树的根节点

#define POOL_SIZE 5000000 //节点池的大小

treeNode_advance node_pool[POOL_SIZE]; //预分配的节点池
size_t pool_index = 0; //当前节点池的索引


uint32_t net_to_num(const char* ip_str){
    //将ip地址转为无符号整数
    uint32_t ip_num = 0;
    int a, b, c, d;
    if (sscanf(ip_str, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        fprintf(stderr,"Invalid IP address format: %s\n", ip_str);
        return 0;
    }
    ip_num = (a << 24) | (b << 16) | (c << 8) | d;
    return ip_num;
}


treeNode* create_new_node(treeNode* p, NodeType type){
    //初始化前缀树的根节点
    treeNode* node = (treeNode*)malloc(sizeof(treeNode));
    if (node == NULL) {
        fprintf(stderr,"Memory allocation failed\n");
        return NULL;
    }
    node->left = NULL;
    node->right = NULL;
    node->port = -1; //默认端口为-1
    node->type = type;
    return node;
}

treeNode_advance* create_new_node_advance(treeNode_advance* p, NodeType type){
        //初始化前缀树的根节点
    treeNode_advance* node = &node_pool[pool_index++];
    node->port2 = -1;
    node->port1 = -1;
    node->type = type;
    for(int i = 0; i < 4; i++) node->child[i] = NULL;

    return node;

}


void insert_tree(treeNode* parent,uint32_t ip, int  prefix_len, int port){
    treeNode* cur = parent;

    for(int i = 0; i < prefix_len; i++){
        int bit = (ip >> (31 - i)) & 1; //从高位到低位取出每一位
        if(bit){
            if(cur->right == NULL){
                cur->right = create_new_node(cur,  I_NODE);
            }
            cur = cur->right;
        }else{
            if(cur->left == NULL){
                cur->left = create_new_node(cur, I_NODE);
            }
            cur = cur->left;
        }
    }
    cur->port = port;
    cur->type = M_NODE;

}


void insert_tree_advance(treeNode_advance* parent, uint32_t ip, int prefix_len, int port){
    treeNode_advance* cur = parent;
    int steps = prefix_len / 2; // 每次处理2位
    int remaining = prefix_len % 2; // 处理完后剩余的位数
    int shift = 30; 
    for(int i = 0; i < steps; i++,shift -= 2){
        int idx = (ip >> shift) & 0x3; // 取出当前两位
        if(cur->child[idx] == NULL){
            cur->child[idx] = create_new_node_advance(cur,I_NODE);
        }
        cur = cur->child[idx];
    }
    if(remaining > 0){// 如果还有剩余位
        shift = 30 - 2 * steps;
        int bit = (ip >> shift) & 0x2;  // 这里AI找不出来问题，其实是应该取2位然后按照前一位填充
        int idx1 = bit;
        int idx2 = idx1 | 0x1; // 计算下一个索引
        if(cur->child[idx1] == NULL){
                cur->child[idx1] = create_new_node_advance(cur, I_NODE);
        }
        cur->child[idx1]->port1 = port;
        cur->child[idx1]->type = M_NODE;

        if(cur->child[idx2] == NULL){
                cur->child[idx2] = create_new_node_advance(cur, I_NODE);
        }
        cur->child[idx2]->port1 = port;
        cur->child[idx2]->type = M_NODE;
    }
    else{// 如果没有剩余位，直接设置端口
        cur->port2 = port;
        cur->type = M_NODE;
    }
    
}

//读取lookup_file测试数据集，将IP地址转为无符号整数并通过指针返回
// return an array of ip represented by an unsigned integer, size is TEST_SIZE
uint32_t* read_test_data(const char* lookup_file){
    FILE* fp = fopen(lookup_file, "r");
    if (fp == NULL) {
        fprintf(stderr,"Failed to open file: %s\n", lookup_file);
        return NULL;
    }
    uint32_t* ip_vec = (uint32_t*)malloc(sizeof(uint32_t) * TEST_SIZE);
    if (ip_vec == NULL) {
        fprintf(stderr,"Memory allocation failed\n");
        fclose(fp);
        return NULL;
    }
    char line[20];
    for (int i = 0; fgets(line, sizeof(line), fp) != NULL; i++) {
        ip_vec[i] = net_to_num(line);
    }
    fclose(fp);
    return ip_vec;
}

//创建基本的ip前缀树，表项来自forward_file，每一行格式为(ip, mask_len, port)
// Constructing an advanced trie-tree to lookup according to `forward_file`
void create_tree(const char* forward_file){
    FILE* fp = fopen(forward_file, "r");
    if (fp == NULL) {
        fprintf(stderr,"Failed to open file: %s\n", forward_file);
        return;
    }
    root = create_new_node(NULL,  I_NODE); //创建根节点
    char line[100];
    while (fgets(line, sizeof(line), fp) != NULL) {
        uint32_t ip;
        char ip_str[20];
        int prefix_len, port;
        if (sscanf(line, "%19s %d %d", ip_str, &prefix_len, &port) != 3) {
            fprintf(stderr,"Invalid line format: %s\n", line);
            continue;
        }
        ip = net_to_num(ip_str);
        //前缀二叉树
        insert_tree(root, ip, prefix_len, port);

    }
    fclose(fp);
}

//根据create_tree创建的前缀树，去查找ip_vec中每个ip对应的port，保存到数组内并返回，查询不到的条目设置结果为-1，数组长度见tree.h
// Look up the ports of ip in file `lookup_file` using the basic tree
uint32_t *lookup_tree(uint32_t* ip_vec){
    //不sleep达不到两倍的速度
    treeNode* cur;

    for(int i = 0; i < TEST_SIZE; i++){
        uint32_t ip = ip_vec[i];
        cur = root; 
        int port = -1;//默认端口为-1，保证没找到port时返回-1
        for(int j = 0; j < 32; j++){
            int bit = (ip >> (31 - j)) & 1; //从高位到低位取出每一位
            if(bit){
                if(cur->right == NULL){
                    break;
                }
                cur = cur->right;
            }else{
                if(cur->left == NULL){
                    break;
                }
                cur = cur->left;
            }
            if(cur->type == M_NODE){
                port = cur->port;
            }
        }
        ip_vec[i] = port;
    }
    return ip_vec;
}

//功能同create_tree，优化版本
// Constructing an advanced trie-tree to lookup according to `forwardingtable_filename`
void create_tree_advance(const char* forward_file){
    FILE* fp = fopen(forward_file, "r");
    if (fp == NULL) {
        fprintf(stderr,"Failed to open file: %s\n", forward_file);
        return;
    }
    root_advance = create_new_node_advance(NULL,  I_NODE); //创建根节点
    char line[100];
    while (fgets(line, sizeof(line), fp) != NULL) {
        uint32_t ip;
        char ip_str[20];
        int prefix_len, port;
        if (sscanf(line, "%19s %d %d", ip_str, &prefix_len, &port) != 3) {
            fprintf(stderr,"Invalid line format: %s\n", line);
            continue;
        }
        ip = net_to_num(ip_str);
        //前缀二叉树
        insert_tree_advance(root_advance, ip, prefix_len, port);

    }
    fclose(fp);
}

//功能同lookup_tree，优化版本
// Look up the ports of ip in file `lookup_file` using the advanced tree
uint32_t *lookup_tree_advance(uint32_t* ip_vec){
    for(int i = 0; i < TEST_SIZE; i++){
        uint32_t ip = ip_vec[i];
        treeNode_advance* cur = root_advance;
        int port = -1;
        for (int shift = 30; shift >= 0; shift -= 2) {
            int idx = (ip >> shift) & 0x3;
            if (!cur->child[idx]) break;
            cur = cur->child[idx];
            if (cur->type == M_NODE)
                port = cur->port2 != -1 ? cur->port2 : cur->port1;
        }
        ip_vec[i] = port;
    }
    return ip_vec;
}