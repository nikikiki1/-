#ifndef __TREE_H__
#define __TREE_H__

#include <stdint.h>
#include <stdio.h>

// do not change it
#define TEST_SIZE 100000

typedef enum{ I_NODE, M_NODE} NodeType;


typedef struct treeNode{
    int port;
    NodeType type;
    struct treeNode *left;
    struct treeNode *right;
} treeNode;

typedef struct treeNode_advance{
    int port2;
    int port1;//只匹配一位的
    NodeType type;
    struct treeNode_advance* child[4];
}treeNode_advance;

void create_tree(const char*);
uint32_t *lookup_tree(uint32_t *);
void create_tree_advance(const char*);
uint32_t *lookup_tree_advance(uint32_t *);

uint32_t* read_test_data(const char* lookup_file);

#endif
