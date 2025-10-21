#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <stdbool.h>
#include <stdio.h>

#include "ftl.h"

#define MAX_MAPPING_ENTRIES (64 * 1000 * 1000)


static uint64_t memoryUsed = 0;
static uint64_t memoryMax = 0;



typedef struct {
    uint64_t ppn[MAX_MAPPING_ENTRIES]; // 记录哪些ppn组在cache中
} FTL;

static FTL *ftl = NULL;

void FTLInit() {
    ftl = (FTL*)malloc(sizeof(FTL));
    memoryUsed += sizeof(FTL);
}

void FTLDestroy() {
    free(ftl);
    ftl = NULL;
}

uint64_t FTLRead(uint64_t lba) {
    
    
    return ftl->ppn[lba]; // 未找到
}


bool FTLModify(uint64_t lba) {
    ftl->ppn[lba]= 4*lba;
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
    printf("algorithmRunningDuration:\t %f ms\n", during);
    printf("Max memory used:\t\t %llu B\n", (unsigned long long)memoryMax);

    return RETURN_OK;
}