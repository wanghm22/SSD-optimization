#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <stdbool.h>

#include "ftl.h"

#define MAX_MAPPING_ENTRIES (64 * 1000 * 1000)


static uint64_t memoryUsed = 0;
static uint64_t memoryMax = 0;

typedef struct {
    uint64_t ppn[128];
} Block;

typedef struct {
    Block **items;       // 存储Block指针的数组
    int size;           // 当前元素数量
} vector;

// 初始化vector
vector* vector_init() {
    vector *v = (vector*)malloc(sizeof(vector));
    if (!v) return NULL;
    
    v->size = 0;
    v->items = NULL;  // 初始化为空
    
    memoryUsed += sizeof(vector);
    
    return v;
}



// 释放vector
void vector_free(vector *v) {
    if (v == NULL) return;
    
    // 释放所有Block元素
    for (int i = 0; i < v->size; i++) {
        if (v->items[i] != NULL) {
            free(v->items[i]);
            memoryUsed -= sizeof(Block);
        }
    }
    
    // 释放items数组
    if (v->items != NULL) {
        memoryUsed -= sizeof(Block*) * v->size;
        free(v->items);
    }
    
    memoryUsed -= sizeof(vector);
    free(v);
}

// 在末尾添加元素
bool vector_push_back(vector *v, Block *element) {
    // 重新分配items数组，增加一个位置
    Block **new_items = (Block**)realloc(v->items, sizeof(Block*) * (v->size + 1));
    if (!new_items) return false;
    
    // 更新内存使用统计
    if (v->items == NULL) {
        memoryUsed += sizeof(Block*) * (v->size + 1);
    } else {
        memoryUsed += sizeof(Block*);  // 每次只增加一个指针的大小
    }
    
    v->items = new_items;
    
    // 分配新Block的内存
    v->items[v->size] = (Block*)malloc(sizeof(Block));
    if (!v->items[v->size]) {
        return false;
    }
    
    memoryUsed += sizeof(Block);
    
    // 拷贝数据
    memcpy(v->items[v->size], element, sizeof(Block));
    v->size++;
    
    return true;
}

// 获取指定位置的元素（带边界检查）
Block* vector_at(vector *v, int index) {
    if (index < 0 || index >= v->size) {
        return NULL;
    }
    return v->items[index];
}

// 设置指定位置的元素（如果不存在则创建）
bool vector_set(vector *v, int index, Block *element) {
    if (index < 0) return false;
    
    // 如果索引超出当前大小，需要扩展vector
    while (index >= v->size) {
        Block empty_block = {0};  // 初始化为全0
        if (!vector_push_back(v, &empty_block)) {
            return false;
        }
    }
    
    // 拷贝数据到现有位置
    memcpy(v->items[index], element, sizeof(Block));
    return true;
}

static vector *ftl = NULL;

void FTLInit() {
    ftl = vector_init();
}

void FTLDestroy() {
    vector_free(ftl);
    ftl = NULL;
}

/**
 * @brief  获取 lba 对应的 ppn
 * @param  lba            逻辑地址
 * @return uint64_t       返回物理地址
 */
uint64_t FTLRead(uint64_t lba) {   
    uint64_t theidx = lba / 128;
    
    if (theidx >= (uint64_t)ftl->size) {
        // 读取未分配的块，返回0或错误值
        return 0;
    }
    
    Block *block = vector_at(ftl, theidx);
    if (!block) {
        return 0;
    }
    
    return block->ppn[lba % 128];
}

/**
 * @brief  记录 FTL 映射 lba->ppn
 * @param  lba            逻辑地址
 * @param  ppn            物理地址
 * @return bool           返回
 */
bool FTLModify(uint64_t lba, uint64_t ppn) {    
    uint64_t theidx = lba / 128;
    uint32_t offset = lba % 128;
    
    Block *block = NULL;
    
    if (theidx < (uint64_t)ftl->size) {
        // 块已存在
        block = vector_at(ftl, theidx);
    } else {
        // 需要创建新块
        Block new_block = {0};  // 初始化为全0
        if (!vector_set(ftl, theidx, &new_block)) {
            return false;
        }
        block = vector_at(ftl, theidx);
    }
    
    if (!block) {
        return false;
    }
    
    block->ppn[offset] = ppn;
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

    FTLInit();

    // 记录开始时间
    gettimeofday(&start, NULL);

    for (uint32_t i = 0; i < ioVector->len; ++i) {
        if (ioVector->ioArray[i].type == IO_READ) {
            ret = FTLRead(ioVector->ioArray[i].lba);
            fprintf(file, "%llu\n", ret);
        } else {
            FTLModify(ioVector->ioArray[i].lba, ioVector->ioArray[i].ppn);
        }
        memoryMax = MAX(memoryMax, memoryUsed);
    }

    // 记录结束时间
    gettimeofday(&end, NULL);

    FTLDestroy();

    fclose(file);
    
    // 计算秒数和微秒数
    seconds = end.tv_sec - start.tv_sec;
    useconds = end.tv_usec - start.tv_usec;

    // 总微秒数
    during = ((seconds) * 1000000 + useconds);
    printf("algorithmRunningDuration:\t %f ms\n", during);
    printf("Max memory used:\t\t %llu B\n", memoryMax);

    return RETURN_OK;
}