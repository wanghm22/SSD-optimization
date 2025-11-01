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
    int size;
    int idx; //表示Cache存储的是第几组PPN
    uint64_t valid;
    uint64_t pba;
} cache_entry;

typedef struct {
    uint64_t valid;
    uint64_t pba;
} ppn_entry;

typedef struct {
    ppn_entry *ppn;
    cache_entry cache[CACHE_SIZE];
    uint64_t incache[CACHE_GROUP]; // 记录哪些ppn组在cache中
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
    for (int i = 0; i < CACHE_SIZE; ++i) {
        ftl->cache[i].idx = -1;
        ftl->cache[i].size = 0;
        ftl->cache[i].valid = 0;
        ftl->cache[i].pba = (i+PPN_COUNT)*1000;
    }
    for(int i=0;i<CACHE_GROUP;++i){
        ftl->incache[i]=0;
    }
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
    
    uint64_t ppn_index = lba / BLOCKS_PER_PAGE;
    int offset = lba % BLOCKS_PER_PAGE;
    int ppn_group = ppn_index / 64;
    int ppn_offset = ppn_index % 64;
    if((ftl->incache[ppn_group]&(1<<ppn_offset))!=0){
        for(int i=0;i<CACHE_SIZE;++i){
            if(ftl->cache[i].idx==ppn_index){
                if((ftl->cache[i].valid&(1<<offset))!=0){return ftl->cache[i].pba+offset;}
            } 
        }
    }
    // 首先在cache中查找
    
    
    // 在PPN中查找
    
    return ftl->ppn[ppn_index].pba + offset ;
    
    
     // 未找到
}

int CleanCache() {
    int max_size = 0;
    int max_index = -1;
    
    // 找到最大的cache条目
    for (int i = 0; i < CACHE_SIZE; ++i) {
        if (ftl->cache[i].size > max_size) {
            max_size = ftl->cache[i].size;
            max_index = i;
        }
    }
    
    if (max_index == -1 || max_size == 0) {
        return 0;
    }
    
    
    int ppn_group= ftl->cache[max_index].idx / 64;
    int ppn_offset = ftl->cache[max_index].idx % 64;
    ftl->incache[ppn_group] &= ~(1<<ppn_offset);//将cache中的ppn组标记为不在cache中
    // 将cache内容写回PPN
    uint64_t ppn_index = ftl->cache[max_index].idx;
    uint64_t thepba =ftl->ppn[ppn_index].pba;
    ftl->ppn[ppn_index].pba = ftl->cache[max_index].pba;
    
    // 释放cache内存
    ftl->cache[max_index].idx = -1;
    ftl->cache[max_index].size = 0;
    ftl->cache[max_index].pba = thepba;
    ftl->cache[max_index].valid = 0;
    
    return max_index;
}

bool FTLModify(uint64_t lba) {
    if (!ftl) return false;
    
    uint64_t ppn_index = lba / BLOCKS_PER_PAGE;
    uint64_t offset = lba % BLOCKS_PER_PAGE;
    
    if (ppn_index >= PPN_COUNT) {
        return false; // 越界
    }
    if((ftl->ppn[ppn_index].valid&(1<<offset))==0){
        ftl->ppn[ppn_index].valid |= (1<<offset);
        
        return true;}//不是重写，直接秒
    int ppn_group = ppn_index / 64;
    int ppn_offset = ppn_index % 64;
    if((ftl->incache[ppn_group]&(1<<ppn_offset))!=0){//本组在cache中
        for(int i=0;i<CACHE_SIZE;++i){
            if(ftl->cache[i].idx==ppn_index){
                if((ftl->cache[i].valid&(1<<offset))==0){
                    ftl->cache[i].valid |= (1<<offset);
                    return true;}
                else{
                    
                    ftl->incache[ppn_group] &= ~(1<<ppn_offset);//将cache中的ppn组标记为不在cache中
    // 将cache内容写回PPN
                    
                    uint64_t thepba =ftl->ppn[ppn_index].pba;
                    ftl->ppn[ppn_index].pba = ftl->cache[i].pba;
    
    // 释放cache内存
                    ftl->cache[i].idx = -1;
                    ftl->cache[i].size = 0;
                    ftl->cache[i].pba = thepba;
                    ftl->cache[i].valid = 0;
                    return true;
                }
            }
    }
    }
    
    for(int i=0;i<CACHE_SIZE;++i){
        if(ftl->cache[i].size==0){
            ftl->cache[i].idx=ppn_index;
            ftl->cache[i].size=1;

            ftl->cache[i].valid|=1<<offset;
            ftl->incache[ppn_group]|=1<<ppn_offset;//将cache中的ppn组标记为在cache中
            return true;
        }
    }
    int theindex = CleanCache();
    ftl->cache[theindex].idx=ppn_index;
    ftl->cache[theindex].size=1;

    ftl->cache[theindex].valid|=1<<offset;
    ftl->incache[ppn_group]|=1<<ppn_offset;//将cache中的ppn组标记为在cache中
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