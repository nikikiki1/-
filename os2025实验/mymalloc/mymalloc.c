#include <mymalloc.h>
#include <sys/syscall.h>
#include <unistd.h>


spinlock_t big_lock;

#define NUM_ARENAS 256
#define CACHE_SIZE 32

#define MIN_MALLOC 4096*4096

//为什么一直提示找不到malloc和free
//因为freestanding里没实现

struct vmalloc_block{//* vmalloc_block is used to manage the vmalloc memory block */
    size_t length;
    void *start;//用于最后释放
    struct vmalloc_block *next;
    int used_blocks;
    struct header* child;//仅做表头
    struct global_list *arena;
};

struct header{//* header is used to manage the memory block */
    size_t size;
    struct header *next;
    struct header *prev;
    size_t used;
    struct vmalloc_block *vm_from;
    struct header *next_free;
    struct header *prev_free;
};

struct global_list{//* global is used to manage the memory block */
    struct vmalloc_block *vmalloc_head;
    struct header *free_head;
    spinlock_t lock;
};

// struct __thread struct{
//     struct header* cache[CACHE_SIZE];
//     int count;
// }tls_cache ={0};

//static struct global_list global = {NULL};//* global is used to manage the memory block */
static struct global_list global[NUM_ARENAS] = {0};


static inline pid_t gettid(void) {
    pid_t tid;
    __asm__ volatile (
        "syscall"
        : "=a" (tid)
        : "0" (SYS_gettid)
        : "rcx", "r11", "memory"
    );
    return tid;
}

//保证mymalloc八字节对齐
static size_t addr_align(size_t size,size_t alig){
    return (size + alig-1) & ~(alig-1);// Align to 8 bytes
}


//已经被使用的会从head表中删去
void *mymalloc(size_t size) {
    if(size == 0){
        //printf("has no memory\n");
        return NULL;
    }
    // if(tls_cache.count > 0){
    //     struct header* ptr = tls_cache.cache[--tls_cache.count];
    //     ptr->used = 1;
    //     return ptr;
    // }
    int arena_id = gettid() % NUM_ARENAS;    
      size_t align_size = addr_align(size,8);// Align to 8 bytes
    size_t total_size = sizeof(struct header) + align_size;//total_size计算header本身加上所需size的结果




    spin_lock(&global[arena_id].lock);
   //疑似老师修改了标准，隔了一个星期提交了一份一模一样的代码，AC了
    // //注释掉下面size>4096的代码,注释掉myfree能过5个测试用例

    // if(size >= 4096){
    //     size_t eventual_size = addr_align(total_size + sizeof(struct vmalloc_block) ,4096 );
    //     struct vmalloc_block *head = vmalloc(NULL,eventual_size);
    //     if(head == NULL){
    //         //printf("vmalloc failed\n");
    //         spin_unlock(&global[arena_id].lock);
    //         return NULL;
    //     }
    //     head->start = head;
    //     head->length = eventual_size;
    //     head->used_blocks = 1;
    //     head->next = global[arena_id].vmalloc_head;
    //     global[arena_id].vmalloc_head = head;
    //     head->arena = &global[arena_id];


    //     struct header* ch = (struct header *)((char *)head + sizeof(struct vmalloc_block));
    //     ch->size = eventual_size - sizeof(struct vmalloc_block);
    //     ch->next = NULL;
    //     ch->prev = NULL;

    //     ch->vm_from  = head;
    //     head->child = ch;
    //     ch->used = 1;

    //     ch->next_free = NULL;
    //     ch->prev_free = NULL;

    //     spin_unlock(&global[arena_id].lock);
    //     return (void *)((char *)ch + sizeof(struct header));

    // }


    //对齐
  

//在已经有的块中寻找
    struct header* cur = global[arena_id].free_head;

    while(cur != NULL){
        if(cur->size >= total_size){
            struct vmalloc_block *head1 = cur->vm_from;

            if(cur->size > total_size + sizeof(struct header) + 16){//还能分出一个新的header
                struct header *new1 = (struct header *)((char *)cur + total_size);
                new1->size = cur->size - total_size;
                new1->next = cur->next;
                new1->prev = cur;
                new1->vm_from = head1;
                new1->used = 0;

                if(cur->next != NULL){
                    cur->next->prev = new1;
                }

                cur->next = new1;
                cur->size = total_size;
                cur->used = 1;


                new1->next_free = cur->next_free;
                if(cur->next_free != NULL){
                    cur->next_free->prev_free = new1;
                }
                new1->prev_free = cur->prev_free;
                if(cur->prev_free != NULL){
                    cur->prev_free->next_free = new1;
                }else{
                    global[arena_id].free_head = new1;
                }


                cur->prev_free = NULL;
                cur->next_free = NULL;
               
            }
            else{
                //当前内存块不能再分出一个新块，使用整块
                cur->used = 1;

                if(cur->prev_free != NULL){
                    cur->prev_free->next_free = cur->next_free;
                }else{
                    global[arena_id].free_head = cur->next_free;
                }

                if(cur->next_free != NULL){
                    cur->next_free->prev_free = cur->prev_free;
                }

                cur->next_free = NULL;
                cur->prev_free = NULL;
            }

            head1->used_blocks++;
            spin_unlock(&global[arena_id].lock);
            return (void *)((char *)cur + sizeof(struct header));
        }
        cur = cur->next_free;
    }

    

    //没有找到合适的空闲内存块，申请新的内存块
   size_t metadate_size = addr_align(sizeof(struct vmalloc_block),8);
  //  size_t total_vm_size = addr_align(total_size + metadate_size,4096);//vmalloc的length需要为4096的倍数
    size_t total_vm_size = MIN_MALLOC;
    struct vmalloc_block *vm_head = (struct vmalloc_block*)vmalloc(NULL,total_vm_size);
    if(vm_head == NULL){
        //printf("vmalloc failed\n");
        spin_unlock(&global[arena_id].lock);
        return NULL;
    }
   

    vm_head->start = vm_head;
    vm_head->length = total_vm_size;
    vm_head->next = global[arena_id].vmalloc_head;
    vm_head->used_blocks = 1;
    vm_head->arena = &global[arena_id];
   
    global[arena_id].vmalloc_head = vm_head;//新申请部分放入头部

    void* usable_area = (struct header *)((char *)vm_head + metadate_size);//记录可用内存大块
    size_t usable_area_size = total_vm_size - metadate_size;


    struct header *vm_child = (struct header *)usable_area;
    vm_child->size = usable_area_size;//这里不需要减header
    vm_child->next = NULL;
    vm_child->prev = NULL;

    vm_child->vm_from = vm_head;
    vm_head->child = vm_child;


    vm_child->used = 0;
    
    struct header* alloc_ptr = (struct header*)usable_area;
    if(alloc_ptr->size > total_size + sizeof(struct header) + 8){
        struct header *new_child = (struct header *)((char *)alloc_ptr + total_size);
        new_child->size = alloc_ptr->size - total_size;
        new_child->next = NULL;
        new_child->prev = alloc_ptr;
        new_child->vm_from = vm_head;
        new_child->used = 0;

        new_child->next_free = global[arena_id].free_head;
        new_child->prev_free = NULL;
        if(global[arena_id].free_head){
            global[arena_id].free_head->prev_free = new_child;
        }
        global[arena_id].free_head = new_child;

        alloc_ptr->next = new_child;
        alloc_ptr->size = total_size;
        alloc_ptr->vm_from = vm_head;
        
    }
    alloc_ptr->used = 1;

    alloc_ptr->next_free = NULL;
    alloc_ptr->prev_free = NULL;

    spin_unlock(&global[arena_id].lock);

    return (void *)((char *)alloc_ptr + sizeof(struct header));
}



//归还给head表，但如果一整块都没有被使用，则整个释放
//需要优化，有过每次归还插入头部，怎么实现合并？
void myfree(void *ptr) {
    
    if(ptr == NULL){
        //printf("ptr == NULL\n");
        return;
    }

 //    spin_lock(&big_lock);
 
    

    struct header *head = (struct header *)((char *)ptr - sizeof(struct header));

    struct vmalloc_block* fro = head->vm_from;

    struct global_list *arena = fro->arena;

    spin_lock(&arena->lock);
    head->used = 0;

    fro->used_blocks--;//减少使用的块数

 //标记为未使用，如果前后未使用，合并   
    struct header *prev = head->prev;
    struct header *next = head->next;

    if(prev != NULL && prev->used == 0 && 
        (char*)prev + prev->size == (char*)head &&
         prev->vm_from == fro){
        //合并前一个块
        prev->size += head->size;//这里不要加上sizeof，因为head的size已经包含了sizeof
        prev->next = next;
        
        if(next != NULL){
            next->prev = prev;
        }
        
    
        if(prev->prev_free != NULL){
            prev->prev_free->next_free = prev->next_free;
        }else{
            arena->free_head = prev->next_free;
        }
        if(prev->next_free != NULL){
            prev->next_free->prev_free = prev->prev_free;
        }


        prev->next_free = NULL;
        prev->prev_free = NULL;
        head->next  = NULL;
        head->prev  = NULL;
        head->vm_from = NULL;
        head->prev_free = NULL;
        head->next_free = NULL;
        head->size = 0;
        head = prev;//调换顺序，让head的prev_free为空，方便插入
    }   
    if(next != NULL && next->used == 0 && 
        (char*)head + head->size == (char*)next
         && next->vm_from == fro){
        //合并后一个块
        head->size += next->size;
        head->next = next->next;
        if(next->next != NULL){
            next->next->prev = head;
        }

        if(next->prev_free != NULL){
            next->prev_free->next_free = next->next_free;
        }else{
            arena->free_head = next->next_free;
        }

        if(next->next_free != NULL){
            next->next_free->prev_free = next->prev_free;
        }
        next->next_free = NULL;
        next->prev_free = NULL;
        next->next  = NULL;
        next->prev  = NULL;
    }

    head->prev_free = NULL;
    head->next_free = arena->free_head;
    if(arena->free_head != NULL){
        arena->free_head->prev_free = head;
    }
    arena->free_head = head;    
    

    if(fro->used_blocks == 0){
        //注意！！！！！！！！！！！从free_head中移除该大块
        struct header *current = arena->free_head;
        struct header *prev = NULL;
        while (current != NULL) {
            struct header *next = current->next_free;
            if (current->vm_from == fro) {
                if (prev != NULL) {
                   prev->next_free = next;
                } else {
                    arena->free_head = next;
                }
                if (next != NULL) {
                    next->prev_free = prev;
                }
                current->prev_free = NULL;
                current->next_free = NULL;
            } else {
                prev = current;
            }
            current = next;
        }

        //如果该大块没有使用的块了，释放该大块
        if(arena->vmalloc_head == fro){
            arena->vmalloc_head = fro->next;
        }else{
            struct vmalloc_block *prev = arena->vmalloc_head;
            while(prev != NULL && prev->next != fro){
                prev = prev->next;
            }
            if(prev != NULL){
                prev->next = fro->next;
            }
        }
        vmfree(fro,fro->length);
    }


    spin_unlock(&arena->lock);
    
    
}        
    

