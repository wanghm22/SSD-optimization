#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <stdbool.h>
#include <stdio.h>

#include "ftl.h"

#define MAX_MAPPING_ENTRIES (64 * 1000 * 1000)
#define VALIDSIZE 1000000
#define CACHE_SIZE (16)
static uint64_t memoryUsed = 0;
static uint64_t memoryMax = 0;


typedef struct{
    int lba;
    uint64_t ppn;
}cache_entry;
typedef struct {
    uint64_t ppn[MAX_MAPPING_ENTRIES]; // 记录哪些ppn组在cache中
    uint64_t valid[VALIDSIZE];
    uint64_t incache[VALIDSIZE];
    cache_entry cache[CACHE_SIZE];
} FTL;

static FTL *ftl = NULL;

void FTLInit() {
    ftl = (FTL*)malloc(sizeof(FTL));
    for(int i=0;i<MAX_MAPPING_ENTRIES;i++){
        ftl->ppn[i]=i*4;
    }
    for(int i=0;i<VALIDSIZE;i++){
        ftl->valid[i]=0;
        ftl->incache[i]=0;
    }
    for(int i=0;i<CACHE_SIZE;i++){
        ftl->cache[i].lba=-1;
        ftl->cache[i].ppn=(i+MAX_MAPPING_ENTRIES)*4;
    }
        
    memoryUsed += sizeof(FTL);
}

void FTLDestroy() {
    free(ftl);
    ftl = NULL;
}

uint64_t FTLRead(uint64_t lba) {
    int idx=lba/64;
    int offset=lba%64;
    if((ftl->incache[idx]&(1<<offset))!=0){
        for(int i=0;i<CACHE_SIZE;++i){
            if(ftl->cache[i].lba==lba){
                return ftl->cache[i].ppn;
            }
        }
    }
    
    return ftl->ppn[lba]; // 读取ppn
}


bool FTLModify(uint64_t lba) {
    int idx=lba/64;
    int offset=lba%64;
    if((ftl->valid[idx]&(1<<offset))==0){
        ftl->valid[idx]|=1<<offset;
        return true;
    }
    if((ftl->incache[idx]&(1<<offset))==0){
        for(int i=0;i<CACHE_SIZE;++i){
            if(ftl->cache[i].lba==-1){
                ftl->cache[i].lba=lba;
                ftl->incache[idx]|=1<<offset;
              
                return true;
            }
        }
        uint64_t theppn=ftl->ppn[ftl->cache[0].lba];
        ftl->ppn[ftl->cache[0].lba]=ftl->cache[0].ppn;
        ftl->cache[0].ppn=theppn;
        ftl->cache[0].lba=lba;
        return true;
    }
    for(int i=0;i<CACHE_SIZE;++i){
        if(ftl->cache[i].lba==lba){
            uint64_t theppn=ftl->ppn[ftl->cache[i].lba];
        ftl->ppn[ftl->cache[i].lba]=ftl->cache[i].ppn;
        ftl->cache[i].ppn=theppn;
        ftl->cache[i].lba=lba;
            return true;}
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
    float throughput = ioVector->len / during;
    printf("algorithmRunningDuration:\t %f ms\n", throughput);
    printf("Max memory used:\t\t %llu B\n", (unsigned long long)memoryMax);

    return RETURN_OK;
}