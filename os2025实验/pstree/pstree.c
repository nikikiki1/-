#include <stdio.h>
#include <dirent.h>  
#include <stdbool.h> 
#include <stdlib.h> 
#include <string.h> 

#define SIZE 1000

typedef struct ProcessNode{
    int pid;
    char name[100];
    int ppid;
    struct ProcessNode* next;
    struct ProcessNode* children;
    struct ProcessNode* sibling;
}ProcessNode;

ProcessNode *hash_table[SIZE];

int hash(int pid) {//get the hash value of the pid
    return pid % SIZE;
}

void init_hash_table() {//initialize the hash table
    for(int i = 0; i < SIZE; i++) {
        hash_table[i] = NULL;
    }
}

ProcessNode* find_process(int pid) {//find the process in the hash table
    int index = hash(pid);
    ProcessNode *node = hash_table[index];
    while(node != NULL) {
        if(node->pid == pid) {
            return node;
        }
        node = node->next;
    }
    return NULL;
}



ProcessNode* create_process_node(int pid, int ppid,char name[]) {//create a process node
    int index = hash(pid);
    ProcessNode *node = (ProcessNode *)malloc(sizeof(ProcessNode));
    node->pid = pid;
    strncpy(node->name, name, 100);
    node->name[99] = '\0'; 
    node->ppid = ppid;
    node->next = hash_table[index];
    hash_table[index] = node;
    node->children = NULL;
    node->sibling = NULL;
    return node;
}

void add_process_to_tree(ProcessNode* parent, ProcessNode* node) {//add the process to the tree
    if(parent == NULL){
        ProcessNode* root = find_process(1);
        parent = root;
    }
    ProcessNode* child = parent->children;
    if(child == NULL) {
        parent->children = node;
    } else {
        while(child->sibling != NULL) {
            child = child->sibling;
        }
        child->sibling = node;
    }
    
}

bool is_number(char *str){
    for(int i = 0; str[i] != '\0'; i++) {
        if(str[i] < '0' || str[i] > '9') {
            return false;
        }
    }
    return true;
}

int list_process(){
    DIR *dir = opendir("/proc");//proc
    if(dir == NULL) {
        return 1;
    }
    struct dirent *entry;
    while((entry = readdir(dir)) != NULL) {
        if(entry->d_type == DT_DIR && is_number(entry->d_name)) {// Check if the directory name is a number (PID)
            int pid = atoi(entry->d_name);
            if(pid > 1) {
                char path[100];
                sprintf(path, "/proc/%d/status", pid);
                FILE *file = fopen(path, "r");
                if(file == NULL) {//open file failed
                    continue;
                }
                char line[100];
                int ppid = -1;
                char name[100];
                while(fgets(line, 100, file) != NULL) {
                    if (strncmp(line, "Name:", 5) == 0) {
                        strncpy(name, line + 6, 99); 
                        name[strcspn(name, "\n")] = '\0'; //不是这里的问题
                        //sscanf(line, "Name: %s", name);
                    }
                    else if(strncmp(line, "PPid:", 5) == 0) {//可能是这里的问题
                        sscanf(line, "PPid: %d", &ppid);
                    }
                }
                
                fclose(file);
                if(ppid > 0){
                    ProcessNode *p = find_process(ppid);
                    if(p == NULL){
                        p = find_process(1);
                        ppid = 1;
                    }
                    ProcessNode *c = create_process_node(pid, ppid, name);
                    add_process_to_tree(p, c);
                }
                else{
                    ProcessNode *p = find_process(1);
                    ProcessNode *c = create_process_node(pid, 1, name);
                    add_process_to_tree(p, c);
                    
                }
            }
        }
    }
    closedir(dir);
    return 0;
}


void print_process_tree(ProcessNode* node,int level,bool show_pid,bool numeric) {//print the process tree
    if(node == NULL) {
        return;
    }

    for(int i=0;i<level;i++){
        if(i == level - 1) {
            printf("+-");
        } else {
            printf("  ");
        }
    }
    
    printf("%s", node->name);//print the pid
    if(show_pid) {
        printf("(%d)\n", node->pid);
    }
    else {
        printf("\n");
    }

    ProcessNode *child = node->children;
    
    if(numeric){
        ProcessNode* arr[1000]; 
        int count = 0;
        while(child != NULL && count< 1000) {
            arr[count++] = child;
            child = child->sibling;
        }
        
        for(int i = 0; i < count-1; i++) {
            for(int j = 0; j < count-i-1; j++) {
                if(arr[j]->pid > arr[j+1]->pid) {
                    ProcessNode* tmp = arr[j];
                    arr[j] = arr[j+1];
                    arr[j+1] = tmp;
                }
            }
        }
        
        for(int i=0;i<count;i++){
            print_process_tree(arr[i], level + 1, show_pid, numeric);
        }
    }
    else {
        while(child != NULL) {
            print_process_tree(child, level + 1, show_pid, numeric);
            child = child->sibling;
        }
    }
}
 
int main(int argc,char* argv[]) {
    init_hash_table();
    create_process_node(1, -1, "systemd");
    bool show_pids = false;
    bool numeric_sort = false; 
    bool version = false;
    //这里出现错误，list_process()卡住了
    if(list_process() == 1) {//list process failed
        return 1;
    }
    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--show-pids") == 0) {
            show_pids = true;
        }
        else if(strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--numeric-sort") == 0) {
            numeric_sort = true;
        }
        else if(strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            version = true;
        }
        else{
            printf("pstree: invalid option\n");
            return 1;
        }
    }
    if(version){
        printf("pstree Version 23.4\n");
    }else{
        print_process_tree(find_process(1), 0, show_pids, numeric_sort);
    }
    
    return 0;
}
