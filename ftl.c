#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <stdbool.h>
#include <stdint.h>
#include "ftl.h"

#ifdef __GNUC__
__fp16 half_float;
#endif
#define MAX_MAPPING_ENTRIES (64 * 1000 * 1000)
#define NUMBER_OF_SECTORS 250000

static uint64_t memoryUsed = 0;
static uint64_t memoryMax = 0;

typedef struct{
    uint8_t start;
    uint8_t length;
    __fp16 k;
    uint32_t b; 
    bool accuracy;  
}section;//学习段
typedef struct{
    section *sec;//本层学习段数组
    uint8_t size;
}levelsec;//某一层的所有学习段
typedef struct{
    levelsec *sec;
    uint8_t size;
}table;//一个组内（256lba为一组）全部层的学习段
typedef struct{
    uint8_t *data;
    uint8_t size;
}CRB;
typedef struct{
    table t[NUMBER_OF_SECTORS];
    CRB crb[NUMBER_OF_SECTORS];
}FTL;
static FTL *ftl = NULL;

void FTLInit(){
    ftl = malloc(sizeof(FTL));
    memoryUsed += sizeof(FTL);
}

void FTLDestroy() {
    free(ftl);
    ftl = NULL;
}
void Insert(int idx,section sec,int level){
    if(ftl->t[idx].size==level){
        ftl->t[idx].sec=realloc(ftl->t[idx].sec,(level+1)*sizeof(levelsec));
        ftl->t[idx].size+=1;
        ftl->t[idx].sec[level].sec=malloc(sizeof(section));
        ftl->t[idx].sec[level].size=0;
    }
    for(int i=0;i<ftl->t[idx].sec[level].size;++i){//此处有问题，考虑新来的区间与多个区间重叠的情况
        if(!((ftl->t[idx].sec[level].sec[i].start>sec.start+sec.length)||(ftl->t[idx].sec[level].sec[i].start+ftl->t[idx].sec[level].sec[i].length<sec.start))){
            Insert(idx,ftl->t[idx].sec[0].sec[i],level+1);
            ftl->t[idx].sec[level].sec[i]=sec;
            return;
        }
    }
    ftl->t[idx].sec[level].sec=realloc(ftl->t[idx].sec[level].sec,(ftl->t[idx].sec[level].size+1)*sizeof(section));
    ftl->t[idx].sec[level].sec[ftl->t[idx].sec[level].size++]=sec;
    return;
}
uint8_t findincrd(int idx,int offset){
  CRB crb=ftl->crb[idx];
  offset=(uint8_t)offset;
  int i=0;
  int topidx=0;
  while(i<crb.size){
    if(crb.data[topidx]>offset){
        return 0;
    }
    else{
        for(int j=0;j<580;++j){
            if(crb.data[topidx+j]==offset){return j;}
            if(crb.data[topidx+j]==NULL){i+=1;topidx+=j+1;break;} 
        }
    }
  }
  return 0;
}
/**
 * @brief  获取 lba 对应的 ppn
 * @param  lba            逻辑地址
 * @return uint64_t       返回物理地址
 */
uint64_t FTLRead(uint64_t lba) {
    if(!ftl) return 0;
    int idx=lba/256;
    int offset=lba%256;
    table *t=&ftl->t[idx];
    CRB *crb=&ftl->crb[idx];//找到对应组
    for(int i=0;i<t->size;++i){
        for(int j=0;j<t->sec[i].size;++j){
            if(offset>=t->sec[i].sec[j].start&&offset<=t->sec[i].sec[j].start+t->sec[i].sec[j].length){//在这一层找到对应区间
                if(t->sec[i].sec[j].accuracy){//精确映射
                    
                    uint32_t b=t->sec[i].sec[j].b;
                    __fp16 k=t->sec[i].sec[j].k;
                    int step=(int)1/k;
                    if((offset-t->sec[i].sec[j].start)%step==0){
                        return b+(offset-t->sec[i].sec[j].start)/step*4096;
                }
                else{break;}
            }
            else{//近似映射
                uint8_t theidx=findincrd(idx,offset);
                if(theidx==0){break;}
                return t->sec[i].sec[j].b+theidx*4096;
            }
        }
    }
}
return 0;
}
/**
 * @brief  记录 FTL 映射 lba->ppn
 * @param  lba            逻辑地址
 * @param  ppn            物理地址
 * @return bool           返回
 */
bool FTLModify(uint64_t *lba, int size) {
    if (!ftl) return false;
    int idx=lba[0]/256;
    int d=0;
    uint8_t start=0;
    if(size>=2){d=lba[1]-lba[0];}
    for(int i=1;i<size;++i){
        if(lba[i]-lba[i-1]==d&&i<size-1){continue;}
        else if(lba[i]-lba[i-1]!=d&&i<size-1){
            section sec;
            sec.start=start;
            sec.length=(uint8_t)(lba[i-1]-start);
            sec.k=(__fp16)1.0/(sec.length);
            sec.accuracy=true;
            sec.b=2000*idx;
            Insert(idx,sec,0);
            start=lba[i];
        }
        else if(i==size-1&&lba[i]-lba[i-1]==d){
            section sec;
            sec.start=start;
            sec.length=(uint8_t)(lba[i]-start);
            sec.k=(__fp16)1.0/(sec.length);
            sec.accuracy=true;
            sec.b=2000*idx;
            Insert(idx,sec,0);
            
        }
        else if(i==size-1&&lba[i]-lba[i-1]!=d){
            section sec;
            sec.start=start;
            sec.length=(uint8_t)(lba[i-1]-start);
            sec.k=(__fp16)1.0/(sec.length);
            sec.accuracy=true;
            sec.b=2000*idx;
            Insert(idx,sec,0);
            crbinsert(idx,lba[i]);//最后一个lba，不连续的情况
        }
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