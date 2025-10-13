#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <stdbool.h>

#include "ftl.h"

#define MAX_MAPPING_ENTRIES (64 * 1000 * 1000)

static uint64_t memoryUsed = 0;
static uint64_t memoryMax = 0;

typedef enum { RED, BLACK } Color;

// 键值对结构
typedef struct KeyValue {
    int key;
    uint64_t value;
} KeyValue;

// 红黑树节点
typedef struct Node {
    KeyValue data;
    Color color;
    struct Node* left;
    struct Node* right;
    struct Node* parent;
} Node;

// Map 结构
typedef struct {
    Node* root;
    Node* nil;  // 哨兵节点
    int size;
} TreeMap;

// 创建哨兵节点
Node* create_nil_node() {
    Node* nil = (Node*)malloc(sizeof(Node));
    nil->color = BLACK;
    nil->left = nil->right = nil->parent = NULL;
    memoryUsed += sizeof(Node);
    return nil;
}

// 创建新节点
Node* create_node(int key, uint64_t value) {
    Node* new_node = (Node*)malloc(sizeof(Node));
    new_node->data.key = key;
    new_node->data.value = value;
    new_node->color = RED;
    new_node->left = new_node->right = new_node->parent = NULL;
    memoryUsed += sizeof(Node);
    return new_node;
}

// 创建 Map
TreeMap* create_tree_map() {
    TreeMap* map = (TreeMap*)malloc(sizeof(TreeMap));
    memoryUsed += sizeof(TreeMap);
    map->nil = create_nil_node();
    map->root = map->nil;
    map->size = 0;
    return map;
}

// 左旋
void left_rotate(TreeMap* map, Node* x) {
    Node* y = x->right;
    x->right = y->left;
    
    if (y->left != map->nil) {
        y->left->parent = x;
    }
    
    y->parent = x->parent;
    
    if (x->parent == map->nil) {
        map->root = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }
    
    y->left = x;
    x->parent = y;
}

// 右旋
void right_rotate(TreeMap* map, Node* y) {
    Node* x = y->left;
    y->left = x->right;
    
    if (x->right != map->nil) {
        x->right->parent = y;
    }
    
    x->parent = y->parent;
    
    if (y->parent == map->nil) {
        map->root = x;
    } else if (y == y->parent->left) {
        y->parent->left = x;
    } else {
        y->parent->right = x;
    }
    
    x->right = y;
    y->parent = x;
}

// 插入修复
void insert_fixup(TreeMap* map, Node* z) {
    while (z->parent->color == RED) {
        if (z->parent == z->parent->parent->left) {
            Node* y = z->parent->parent->right;
            if (y->color == RED) {
                // Case 1
                z->parent->color = BLACK;
                y->color = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) {
                    // Case 2
                    z = z->parent;
                    left_rotate(map, z);
                }
                // Case 3
                z->parent->color = BLACK;
                z->parent->parent->color = RED;
                right_rotate(map, z->parent->parent);
            }
        } else {
            // 对称情况
            Node* y = z->parent->parent->left;
            if (y->color == RED) {
                z->parent->color = BLACK;
                y->color = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    right_rotate(map, z);
                }
                z->parent->color = BLACK;
                z->parent->parent->color = RED;
                left_rotate(map, z->parent->parent);
            }
        }
    }
    map->root->color = BLACK;
}

// 插入键值对
void tree_map_put(TreeMap* map, int key, uint64_t value) {
    Node* z = create_node(key, value);
    Node* y = map->nil;
    Node* x = map->root;
    
    // 找到插入位置
    while (x != map->nil) {
        y = x;
        if (z->data.key < x->data.key) {
            x = x->left;
        } else if (z->data.key > x->data.key) {
            x = x->right;
        } else {
            // 键已存在，更新值
            x->data.value = value;
            free(z);
            memoryUsed -= sizeof(Node); // 释放未使用的节点
            return;
        }
    }
    
    z->parent = y;
    if (y == map->nil) {
        map->root = z;
    } else if (z->data.key < y->data.key) {
        y->left = z;
    } else {
        y->right = z;
    }
    
    z->left = map->nil;
    z->right = map->nil;
    z->color = RED;
    
    insert_fixup(map, z);
    map->size++;
}

// 查找节点
Node* tree_map_get_node(TreeMap* map, int key) {
    Node* current = map->root;
    while (current != map->nil) {
        if (key == current->data.key) {
            return current;
        } else if (key < current->data.key) {
            current = current->left;
        } else {
            current = current->right;
        }
    }
    return NULL;  // 未找到
}

// 查找值
int tree_map_get(TreeMap* map, int key, uint64_t* value) {
    Node* node = tree_map_get_node(map, key);
    if (node) {
        *value = node->data.value;
        return 1;
    }
    return 0;
}

// 判断键是否存在
int tree_map_contains(TreeMap* map, int key) {
    return tree_map_get_node(map, key) != NULL;
}

// 清空树
void clear_tree(Node* node, Node* nil) {
    if (node != nil) {
        clear_tree(node->left, nil);
        clear_tree(node->right, nil);
        free(node);
        memoryUsed -= sizeof(Node);
    }
}

// 销毁 Map
void free_tree_map(TreeMap* map) {
    clear_tree(map->root, map->nil);
    free(map->nil);
    memoryUsed -= sizeof(Node); // nil 节点的内存
    free(map);
    memoryUsed -= sizeof(TreeMap);
}

static TreeMap *ftl = NULL;

void FTLInit(){
    ftl = create_tree_map();
    // 重置内存统计（如果需要）
    // memoryUsed = 0;
    // memoryMax = 0;
}

void FTLDestroy() {
    if (ftl) {
        free_tree_map(ftl);
        ftl = NULL;
    }
}

/**
 * @brief  获取 lba 对应的 ppn
 * @param  lba            逻辑地址
 * @return uint64_t       返回物理地址
 */
uint64_t FTLRead(uint64_t lba) {
    uint64_t value = 0;
    if (ftl && tree_map_get(ftl, (int)lba, &value)) {
        return value;
    }
    return 0;  // 没有找到
}

/**
 * @brief  记录 FTL 映射 lba->ppn
 * @param  lba            逻辑地址
 * @param  ppn            物理地址
 * @return bool           返回
 */
bool FTLModify(uint64_t lba, uint64_t ppn) {
    if (!ftl) return false;
    tree_map_put(ftl, (int)lba, ppn);
    return true;
}

uint32_t AlgorithmRun(IOVector *ioVector, const char *outputFile) {
    struct timeval start, end;
    long seconds, useconds;
    double during;
    uint64_t ret;
    FILE *file = fopen(outputFile, "w");
    if (!file) {
        perror("Failed to open outputFile");
        exit(EXIT_FAILURE);
    }

    // 初始化 FTL
    FTLInit();
    
    // 重置内存统计
    memoryUsed = 0;
    memoryMax = 0;

    // 记录开始时间
    gettimeofday(&start, NULL);

    for (uint32_t i = 0; i < ioVector->len; ++i) {
        if (ioVector->ioArray[i].type == IO_READ) {
            ret = FTLRead(ioVector->ioArray[i].lba);
            fprintf(file, "%llu\n", (unsigned long long)ret);
        } else {
            FTLModify(ioVector->ioArray[i].lba, ioVector->ioArray[i].ppn);
        }
        if (memoryUsed > memoryMax) {
            memoryMax = memoryUsed;
        }
    }

    // 记录结束时间
    gettimeofday(&end, NULL);

    FTLDestroy();

    fclose(file);
    
    // 计算秒数和微秒数
    seconds = end.tv_sec - start.tv_sec;
    useconds = end.tv_usec - start.tv_usec;

    // 总微秒数
    during = (seconds * 1000000.0 + useconds) / 1000.0;  // 转换为毫秒
    printf("algorithmRunningDuration:\t %f ms\n", during);
    printf("Max memory used:\t\t %llu B\n", (unsigned long long)memoryMax);

    return RETURN_OK;
}