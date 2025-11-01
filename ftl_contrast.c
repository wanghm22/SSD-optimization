#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <stdbool.h>
#include <stdio.h>

#include "ftl.h"

#define MAX_MAPPING_ENTRIES (64 * 1000 * 1000)
#define CACHE_SIZE 16
#define PPN_COUNT 1000000
#define BLOCKS_PER_PAGE 64
#define BLOCK_SIZE 4096
#define CACHE_GROUP 15625

static uint64_t memoryUsed = 0;
static uint64_t memoryMax = 0;



typedef struct {
    uint64_t valid;
    uint64_t pba;
} ppn_entry;

typedef struct {
    ppn_entry *ppn;
    uint64_t cacheppn;
    // 记录哪些ppn组在cache中
} FTL;

static FTL *ftl = NULL;

void FTLInit() {
    ftl = (FTL*)malloc(sizeof(FTL));
    if (!ftl) {
        perror("Failed to allocate FTL");
        exit(EXIT_FAILURE);
    }
    memoryUsed+=sizeof(FTL);
    // 分配PPN数组
    ftl->ppn = (ppn_entry*)calloc(PPN_COUNT, sizeof(ppn_entry));
    if (!ftl->ppn) {
        perror("Failed to allocate PPN array");
        free(ftl);
        exit(EXIT_FAILURE);
    }
    for(int i=0;i<PPN_COUNT;++i){
        ftl->ppn[i].valid=0;
        ftl->ppn[i].pba=i*1000;
    }
    // 初始化cache
    ftl->cacheppn=1000*PPN_COUNT;
    memoryUsed +=PPN_COUNT * sizeof(ppn_entry);
}

void FTLDestroy() {
    if (ftl) {
        // 释放cache中的动态数组
      
        
        // 释放PPN数组
        if (ftl->ppn) {
            free(ftl->ppn);
        }
        
        free(ftl);
        ftl = NULL;
    }
}

uint64_t FTLRead(uint64_t lba) {
    if (!ftl) return 0;
    int index= lba / BLOCKS_PER_PAGE;
    int offset = lba % BLOCKS_PER_PAGE;
    return ftl->ppn[index].pba+offset;
}



bool FTLModify(uint64_t lba) {
    if (!ftl) return false;
    
    int ppn_index = lba / BLOCKS_PER_PAGE;
    int offset = lba % BLOCKS_PER_PAGE;
    
    if (ppn_index >= PPN_COUNT) {
        return false; // 越界
    }
    if((ftl->ppn[ppn_index].valid&(1<<offset))==0){
        ftl->ppn[ppn_index].valid |= (1<<offset);
        
        return true;}//不是重写，直接秒
    uint64_t t=ftl->ppn[ppn_index].pba;
    ftl->ppn[ppn_index].pba=ftl->cacheppn;
    ftl->cacheppn=t;
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
        return RETURN_ERROR;
    }
    memoryUsed = 0;
    memoryMax = 0;
    FTLInit();
    
    

    gettimeofday(&start, NULL);

    for (uint32_t i = 0; i < ioVector->len; ++i) {
        if (ioVector->ioArray[i].type == IO_READ) {
            ret = FTLRead(ioVector->ioArray[i].lba);
            fprintf(file, "%llu\n", (unsigned long long)ret);
        } else {
            FTLModify(ioVector->ioArray[i].lba);
        }
        
        if (memoryUsed > memoryMax) {
            memoryMax = memoryUsed;
        }
    }

    gettimeofday(&end, NULL);

    FTLDestroy();

    fclose(file);
    
    seconds = end.tv_sec - start.tv_sec;
    useconds = end.tv_usec - start.tv_usec;

    during = (seconds * 1000000.0 + useconds) / 1000.0;
    double throughput = (double) ioVector->len / during;
    printf("algorithmRunningDuration:\t %f ms\n", throughput);
    printf("Max memory used:\t\t %llu B\n", (unsigned long long)memoryMax);

    return RETURN_OK;
}