#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <stdbool.h>

#include "ftl.h"

#define MAX_MAPPING_ENTRIES (64 * 1000 * 1000)

static uint64_t memoryUsed = 0;
static uint64_t memoryMax = 0;

static int *lpn = NULL;
typedef struct{
    int size;
    int *vpn;
    uint64_t pba;
}cache;
typedef struct{
    bool valid[16];
    uint64_t pba;
}ppn;
typedef struct{
  ppn ppn[4000000];
  cache cache[16];
}FTL;
static FTL *ftl=NULL;
void FTLInit(){
    ftl=(FTL*)malloc(sizeof(FTL));
    for(int i=0;i<16;++i){
        ftl->cache[i].size=0;
        ftl->cache[i].vpn=NULL;
    }
    memoryUsed += sizeof(FTL);
    // 重置内存统计（如果需要）
    // memoryUsed = 0;
    // memoryMax = 0;
}

void FTLDestroy() {
    if (ftl) {
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
    
    for(int i=0;i<16;++i){
        for(int j=0;j<ftl->cache[i].size;++j){
            if(ftl->cache[i].vpn[j]==lba){
                return ftl->cache[i].pba+lba%16*4096;
            }
        }
    }
    return ftl->ppn[lba/16].pba+lba%16*4096;
}
uint64_t Clean(){
    int maxlen=0;
    int maxidx=-1;
    for(int i=0;i<16;++i){
        if(ftl->cache[i].size>maxlen){
            maxlen=ftl->cache[i].size;
            maxidx=i;
        }
    }
    int idx=ftl->cache[maxidx].vpn[0]/16;
    uint64_t answer= ftl->ppn[idx].pba;
    ftl->ppn[idx].pba=ftl->cache[maxidx].pba;
    for(int i=0;i<16;++i){
        ftl->ppn[idx].valid[i]=0;
    }
    for(int i=0;i<maxlen;++i){
        ftl->ppn[idx].valid[ftl->cache[maxidx].vpn[i]%16]=1;
    }
    memoryUsed -= sizeof(int) * maxlen;
    free(ftl->cache[maxidx].vpn);
    ftl->cache[maxidx].vpn=NULL;
    return answer;
}
/**
 * @brief  记录 FTL 映射 lba->ppn
 * @param  lba            逻辑地址
 * @param  ppn            物理地址
 * @return bool           返回
 */
bool FTLModify(uint64_t lba) {
    int idx=lba/16;
    if(ftl->ppn[idx].pba==0){
        ftl->ppn[idx].pba=10000*idx;
    }
    int offset=lba%16;
    if(!ftl->ppn[idx].valid[offset]){
        ftl->ppn[idx].valid[offset]=true;
        return true;
    }
    else{
        bool find=false;
        for(int i=0;i<16;++i){
            if (ftl->cache[i].vpn!=NULL && ftl->cache[i].vpn[0]/16==idx){
                ftl->cache[i].vpn=(int*)realloc(ftl->cache[i].vpn,sizeof(int));
                ftl->cache[i].vpn[ftl->cache[i].size++]=lba;
                memoryUsed += sizeof(int);
                find=1;break;
            }
        }
        if(!find){
            for(int i=0;i<16;++i){
                if(ftl->cache[i].size==0){
                    ftl->cache[i].vpn=(int*)malloc(sizeof(int));
                    ftl->cache[i].vpn[ftl->cache[i].size++]=lba;
                    ftl->cache[i].pba=10000*(i+4000000);
                    memoryUsed += sizeof(int);
                    find=1;break;
                }
            }
        }
        if(!find){
            uint64_t clean=Clean();
            for(int i=0;i<16;++i){
                if(ftl->cache[i].size==0){
                    ftl->cache[i].vpn=(int*)malloc(sizeof(int));
                    ftl->cache[i].vpn[ftl->cache[i].size++]=lba;
                    ftl->cache[i].pba=clean;
                    memoryUsed += sizeof(int);
                }
            }
        }
        return true;
}
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
            FTLModify(ioVector->ioArray[i].lba);
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