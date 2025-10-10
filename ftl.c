#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <stdbool.h>

#include "ftl.h"

#define MAX_MAPPING_ENTRIES (64 * 1000 * 1000)
#define INITIAL_CAPACITY 1024

static uint64_t memoryUsed = 0;
static uint64_t memoryMax = 0;

typedef struct {
   uint64_t idx;
   uint64_t ppn;
} map_entry;

typedef struct {
    map_entry *data;        // 改为连续内存数组
    uint64_t size;
    uint64_t capacity;
} FTL;

static FTL *ftl = NULL;

void FTLInit() {
    ftl = malloc(sizeof(FTL));
    memoryUsed += sizeof(FTL);
    
    ftl->capacity = INITIAL_CAPACITY;
    ftl->data = calloc(ftl->capacity, sizeof(map_entry));  // 使用calloc初始化为0
    memoryUsed += ftl->capacity * sizeof(map_entry);
    
    ftl->size = 0;
}

void FTLDestroy() {
    if (ftl) {
        if (ftl->data) {
            free(ftl->data);
        }
        free(ftl);
        ftl = NULL;
    }
}

/**
 * @brief  获取 lba 对应的 ppn
 * @param  lba            逻辑地址
 * @return uint64_t       返回物理地址
 */
uint64_t FTLRead(uint64_t lba) {
    // 如果lba在数组范围内且该位置有有效数据，直接返回
    if (lba < ftl->size && ftl->data[lba].idx == lba) {
        return ftl->data[lba].ppn;
    }
    
    // 线性搜索（这种情况应该很少发生）
    for (uint64_t i = 0; i < ftl->size; i++) {
        if (ftl->data[i].idx == lba) {
            return ftl->data[i].ppn;
        }
    }
    
    return 0;  // 未找到映射
}

/**
 * @brief  记录 FTL 映射 lba->ppn
 * @param  lba            逻辑地址
 * @param  ppn            物理地址
 * @return bool           返回
 */
bool FTLModify(uint64_t lba, uint64_t ppn) {
    // 如果lba在现有范围内，直接更新
    if (lba < ftl->size) {
        ftl->data[lba].ppn = ppn;
        ftl->data[lba].idx = lba;  // 确保idx正确设置
        return true;
    }
    
    // 需要扩展数组
    if (lba >= ftl->capacity) {
        uint64_t new_capacity = ftl->capacity * 2;
        while (new_capacity <= lba) {
            new_capacity *= 2;
        }
        
        map_entry *new_data = realloc(ftl->data, new_capacity * sizeof(map_entry));
        if (!new_data) {
            return false;
        }
        
        // 初始化新分配的内存
        memset(new_data + ftl->capacity, 0, (new_capacity - ftl->capacity) * sizeof(map_entry));
        
        memoryUsed += (new_capacity - ftl->capacity) * sizeof(map_entry);
        ftl->data = new_data;
        ftl->capacity = new_capacity;
    }
    
    // 设置新条目
    ftl->data[lba].idx = lba;
    ftl->data[lba].ppn = ppn;
    
    // 更新size
    if (lba >= ftl->size) {
        ftl->size = lba + 1;
    }
    
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