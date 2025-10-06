#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <stdbool.h>

#include "ftl.h"

#define MAX_MAPPING_ENTRIES (64 * 1000 * 1000)

static uint64_t memoryUsed = 0;
static uint64_t memoryMax = 0;

typedef struct {
    uint64_t *items;       // 存储uint64_t值的数组
    int size;              // 当前元素数量
    int capacity;          // 当前容量
} vector;

// 初始化vector
vector* vector_init() {
    vector *v = (vector*)malloc(sizeof(vector));
    if (!v) return NULL;
    
    v->size = 0;
    v->capacity = 0;
    v->items = NULL;
    
    memoryUsed += sizeof(vector);
    if (memoryUsed > memoryMax) memoryMax = memoryUsed;
    
    return v;
}

// 释放vector
void vector_free(vector *v) {
    if (v == NULL) return;
    
    // 释放items数组
    if (v->items != NULL) {
        memoryUsed -= sizeof(uint64_t) * v->capacity;
        free(v->items);
    }
    
    memoryUsed -= sizeof(vector);
    free(v);
    
    if (memoryUsed > memoryMax) memoryMax = memoryUsed;
}

// 确保有足够容量
static bool vector_ensure_capacity(vector *v, int min_capacity) {
    if (min_capacity <= v->capacity) return true;
    
    // 按2的幂次增长，避免频繁realloc
    int new_capacity = v->capacity == 0 ? 16 : v->capacity * 2;
    if (new_capacity < min_capacity) new_capacity = min_capacity;
    
    uint64_t *new_items = (uint64_t*)realloc(v->items, sizeof(uint64_t) * new_capacity);
    if (!new_items) return false;
    
    // 初始化新分配的内存为0
    if (new_capacity > v->capacity) {
        memset(new_items + v->size, 0, sizeof(uint64_t) * (new_capacity - v->size));
    }
    
    memoryUsed += sizeof(uint64_t) * (new_capacity - v->capacity);
    v->items = new_items;
    v->capacity = new_capacity;
    
    if (memoryUsed > memoryMax) memoryMax = memoryUsed;
    
    return true;
}

// 在末尾添加元素
bool vector_push_back(vector *v, uint64_t element) {
    if (!vector_ensure_capacity(v, v->size + 1)) {
        return false;
    }
    
    v->items[v->size] = element;
    v->size++;
    
    return true;
}

// 获取指定位置的元素（带边界检查）
uint64_t vector_at(vector *v, int index) {
    if (index < 0 || index >= v->size) {
        return 0;  // 返回0而不是NULL，因为这是uint64_t
    }
    return v->items[index];
}

// 设置指定位置的元素（如果不存在则扩展）
bool vector_set(vector *v, int index, uint64_t element) {
    if (index < 0) return false;
    
    // 如果索引超出当前大小，需要扩展vector
    if (index >= v->size) {
        if (!vector_ensure_capacity(v, index + 1)) {
            return false;
        }
        v->size = index + 1;
    }
    
    // 直接设置值
    v->items[index] = element;
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
    if (lba >= (uint64_t)ftl->size) {
        // 读取未分配的块，返回0
        return 0;
    }
    
    return vector_at(ftl, lba);
}

/**
 * @brief  记录 FTL 映射 lba->ppn
 * @param  lba            逻辑地址
 * @param  ppn            物理地址
 * @return bool           返回
 */
bool FTLModify(uint64_t lba, uint64_t ppn) {    
    return vector_set(ftl, lba, ppn);
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
    }

    // 记录结束时间
    gettimeofday(&end, NULL);

    FTLDestroy();

    fclose(file);
    
    // 计算秒数和微秒数
    seconds = end.tv_sec - start.tv_sec;
    useconds = end.tv_usec - start.tv_usec;

    // 总微秒数
    during = ((seconds) * 1000000 + useconds) ;  // 转换为毫秒
    printf("algorithmRunningDuration:\t %f ms\n", during);
    printf("Max memory used:\t\t %llu B\n", memoryMax);

    return RETURN_OK;
}