#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "ftl.h"

#define MAX_RECURSION_DEPTH 16
#define NUMBER_OF_SECTORS 250000
#define SECTORS_PER_GROUP 256
#define FLASH_PAGE_SIZE 4096
#define WRITE_BUFFER_SIZE 256
#define INVALID_START 0xFF  // 使用0xFF表示无效（uint8_t的最大值）
#define MAX_HASH_SIZE 16
#define OFFSET 20
#define GROUPNUM 4

static uint64_t memoryUsed = 0;
static uint64_t memoryMax = 0;

typedef struct {
    uint8_t lba;
    uint64_t ppn;
} hash_entry;

typedef struct {
    hash_entry* ghash;
    uint8_t size;
} grouphash;

int hashfunc(int idx) {
    return (idx + OFFSET) % MAX_HASH_SIZE;
}

// 写缓冲区结构
typedef struct {
    uint64_t lba[WRITE_BUFFER_SIZE];
    int count;
    uint32_t next_ppn;
} WriteBuffer;

typedef struct {
    uint8_t start;
    uint8_t length;
    uint8_t step;
    uint32_t b;
} section;

typedef struct {
    section *sec;
    uint8_t size;
} levelsec;

typedef struct {
    levelsec *levels;
    uint8_t level_count;
    grouphash hash[MAX_HASH_SIZE];
    uint64_t valid[GROUPNUM];
} table;

typedef struct {
    table t[NUMBER_OF_SECTORS];
    WriteBuffer write_buffer;
} FTL;

static FTL *ftl = NULL;

uint64_t HashRead(int lba) {
    int idx_ = lba / SECTORS_PER_GROUP;
    int idx = hashfunc(idx_);
    
    if (ftl->t[idx_].hash[idx].ghash == NULL) {
        return 0;
    }
    
    for (int i = 0; i < ftl->t[idx_].hash[idx].size; ++i) {
        if (lba == ftl->t[idx_].hash[idx].ghash[i].lba) {
            return ftl->t[idx_].hash[idx].ghash[i].ppn;
        }
    }
    return 0;
}

void HashWrite(int lba, uint64_t ppn) {
    int idx_ = lba / SECTORS_PER_GROUP;
    int idx = hashfunc(idx_);
    
    // 初始化哈希表或重新分配内存
    if (ftl->t[idx_].hash[idx].ghash == NULL) {
        ftl->t[idx_].hash[idx].ghash = malloc(sizeof(hash_entry));
        if (!ftl->t[idx_].hash[idx].ghash) return;
        ftl->t[idx_].hash[idx].ghash[0].lba = lba;
        ftl->t[idx_].hash[idx].ghash[0].ppn = ppn;
        ftl->t[idx_].hash[idx].size = 1;
        memoryUsed += sizeof(hash_entry);
        return;
    }
    
    // 查找是否已存在
    bool find = false;
    for (int i = 0; i < ftl->t[idx_].hash[idx].size; ++i) {
        if (ftl->t[idx_].hash[idx].ghash[i].lba == lba) {
            ftl->t[idx_].hash[idx].ghash[i].ppn = ppn;
            find = true;
            break;
        }
    }
    
    if (!find) {
        if (ftl->t[idx_].hash[idx].size < MAX_HASH_SIZE) {
            // 重新分配内存以容纳新元素
            hash_entry *new_ghash = realloc(ftl->t[idx_].hash[idx].ghash, 
                                          (ftl->t[idx_].hash[idx].size + 1) * sizeof(hash_entry));
            if (!new_ghash) return;
            
            ftl->t[idx_].hash[idx].ghash = new_ghash;
            ftl->t[idx_].hash[idx].ghash[ftl->t[idx_].hash[idx].size].lba = lba;
            ftl->t[idx_].hash[idx].ghash[ftl->t[idx_].hash[idx].size].ppn = ppn;
            ftl->t[idx_].hash[idx].size++;
            memoryUsed += sizeof(hash_entry);
        }
        // 如果达到MAX_HASH_SIZE限制，不添加新条目
    }
}

void HashDelete(int group, uint8_t lba) {
    int idx = hashfunc(group);
    
    if (ftl->t[group].hash[idx].ghash == NULL) {
        return;
    }
    
    for (int i = 0; i < ftl->t[group].hash[idx].size; ++i) {
        if (ftl->t[group].hash[idx].ghash[i].lba == lba) {
            // 如果是最后一个元素，直接释放整个数组
            if (ftl->t[group].hash[idx].size == 1) {
                free(ftl->t[group].hash[idx].ghash);
                ftl->t[group].hash[idx].ghash = NULL;
                ftl->t[group].hash[idx].size = 0;
                memoryUsed -= sizeof(hash_entry);
            } else {
                // 移动元素并重新分配内存
                for (int j = i; j < ftl->t[group].hash[idx].size - 1; ++j) {
                    ftl->t[group].hash[idx].ghash[j] = ftl->t[group].hash[idx].ghash[j + 1];
                }
                ftl->t[group].hash[idx].size--;
                
                // 重新分配更小的内存
                hash_entry *new_ghash = realloc(ftl->t[group].hash[idx].ghash, 
                                              ftl->t[group].hash[idx].size * sizeof(hash_entry));
                if (new_ghash || ftl->t[group].hash[idx].size == 0) {
                    ftl->t[group].hash[idx].ghash = new_ghash;
                }
                memoryUsed -= sizeof(hash_entry);
            }
            break;
        }
    }
}

void FTLInit() {
    ftl = calloc(1, sizeof(FTL));
    if (!ftl) {
        return;
    }
    
    // 初始化所有结构
    for (int i = 0; i < NUMBER_OF_SECTORS; ++i) {
        ftl->t[i].level_count = 0;
        ftl->t[i].levels = NULL;
        
        for (int j = 0; j < MAX_HASH_SIZE; ++j) {
            ftl->t[i].hash[j].ghash = NULL;
            ftl->t[i].hash[j].size = 0;
        }
        
        for (int j = 0; j < GROUPNUM; ++j) {
            ftl->t[i].valid[j] = 0;
        }
    }
    
    memoryUsed += sizeof(FTL);
    ftl->write_buffer.next_ppn = 1000;
    ftl->write_buffer.count = 0;
}

void FTLDestroy() {
    if (!ftl) return;
    
    for (int i = 0; i < NUMBER_OF_SECTORS; i++) {
        // 释放levels
        for (int j = 0; j < ftl->t[i].level_count; j++) {
            if (ftl->t[i].levels[j].sec) {
                free(ftl->t[i].levels[j].sec);
            }
        }
        if (ftl->t[i].levels) {
            free(ftl->t[i].levels);
        }
        
        // 释放hash
        for (int j = 0; j < MAX_HASH_SIZE; ++j) {
            if (ftl->t[i].hash[j].ghash) {
                free(ftl->t[i].hash[j].ghash);
            }
        }
    }
    free(ftl);
    ftl = NULL;
}

void sort_lba_array(uint64_t *lba_array, int size) {
    for (int i = 1; i < size; i++) {
        uint64_t key = lba_array[i];
        int j = i - 1;
        while (j >= 0 && lba_array[j] > key) {
            lba_array[j + 1] = lba_array[j];
            j--;
        }
        lba_array[j + 1] = key;
    }
}

// 判断section是否有效
bool is_section_valid(section *sec) {
    return sec->start != INVALID_START;
}

// 重叠检测函数
bool is_overlap(section *a, section *b) {
    if (!is_section_valid(a) || !is_section_valid(b)) {
        return false;
    }
    
    uint8_t a_end = (a->start + a->length) ;
    uint8_t b_end =  (b->start + b->length) ;
    
    return !(a->start > b_end || a_end < b->start);
}

// 简化的Insert函数
void Insert(int idx, section new_sec, int start_level) {
    if (!ftl || idx < 0 || idx >= NUMBER_OF_SECTORS) {
        return;
    }
    
    int current_level = start_level;
    section current_sec = new_sec;
    
    while (current_level < MAX_RECURSION_DEPTH) {
        // 确保有足够的层级
        while (ftl->t[idx].level_count <= current_level) {
            uint8_t new_count = ftl->t[idx].level_count + 1;
            levelsec *new_levels = realloc(ftl->t[idx].levels, new_count * sizeof(levelsec));
            if (!new_levels) return;
            ftl->t[idx].levels = new_levels;
            
            ftl->t[idx].levels[ftl->t[idx].level_count].sec = NULL;
            ftl->t[idx].levels[ftl->t[idx].level_count].size = 0;
            ftl->t[idx].level_count = new_count;
            memoryUsed += sizeof(levelsec);
        }
        
        levelsec *current_level_ptr = &ftl->t[idx].levels[current_level];
        section *conflict_sec = NULL;
        int conflict_index = -1;
        
        // 在当前层查找冲突的section
        for (int i = 0; i < current_level_ptr->size; i++) {
            section *existing_sec = &current_level_ptr->sec[i];
            if (is_section_valid(existing_sec) && is_overlap(existing_sec, &current_sec)) {
                conflict_sec = existing_sec;
                conflict_index = i;
                break;
            }
        }
        
        if (conflict_sec != NULL) {
            // 保存冲突的section，准备移到下一层
            section temp_sec = *conflict_sec;
            
            // 无效化当前层的冲突section
            conflict_sec->start = INVALID_START;
            
            // 插入当前section到当前层
            // 重新分配内存以容纳新元素
            uint8_t new_size = current_level_ptr->size + 1;
            section *new_secs = realloc(current_level_ptr->sec, new_size * sizeof(section));
            if (!new_secs) return;
            current_level_ptr->sec = new_secs;
            current_level_ptr->sec[current_level_ptr->size] = current_sec;
            current_level_ptr->size = new_size;
            memoryUsed += sizeof(section);
            
            // 将冲突的section作为下一轮要处理的section
            current_sec = temp_sec;
            current_level++;
        } else {
            // 没有冲突，直接插入当前层
            // 重新分配内存以容纳新元素
            uint8_t new_size = current_level_ptr->size + 1;
            section *new_secs = realloc(current_level_ptr->sec, new_size * sizeof(section));
            if (!new_secs) return;
            current_level_ptr->sec = new_secs;
            current_level_ptr->sec[current_level_ptr->size] = current_sec;
            current_level_ptr->size = new_size;
            memoryUsed += sizeof(section);
            break;
        }
    }
}

// 检查写缓冲区中是否包含指定的LBA
bool is_lba_in_write_buffer(uint64_t lba) {
    if (!ftl || ftl->write_buffer.count == 0) {
        return false;
    }
    
    for (int i = 0; i < ftl->write_buffer.count; i++) {
        if (ftl->write_buffer.lba[i] == lba) {
            return true;
        }
    }
    return false;
}

// ProcessWriteBuffer函数
void ProcessWriteBuffer() {
    if (!ftl || ftl->write_buffer.count == 0) return;
    
    sort_lba_array(ftl->write_buffer.lba, ftl->write_buffer.count);
    uint32_t current_ppn = ftl->write_buffer.next_ppn;
    
    int idx = 0;
    while (idx < ftl->write_buffer.count) {
        int current_group = ftl->write_buffer.lba[idx] / SECTORS_PER_GROUP;
        
        // 找到当前组的结束位置
        int group_end = idx;
        for (int i = idx + 1; i < ftl->write_buffer.count; i++) {
            if (ftl->write_buffer.lba[i] / SECTORS_PER_GROUP != current_group) {
                group_end = i - 1;
                break;
            }
            group_end = i;
        }
        
        // 处理当前组内的所有连续序列
        int group_idx = idx;
        while (group_idx <= group_end) {
            section sec;
            sec.start = ftl->write_buffer.lba[group_idx] % SECTORS_PER_GROUP;
            sec.b = current_ppn;
            
            // 检查是否是单个元素
            if (group_idx == group_end) {
                int sidx = sec.start / 64;
                int offsetx = sec.start % 64;
                
                // 如果之前有映射，先删除
                if ((ftl->t[current_group].valid[sidx] & (1ULL << offsetx)) != 0) {
                    HashDelete(current_group, sec.start);
                }
                
                ftl->t[current_group].valid[sidx] |= (1ULL << offsetx);
                HashWrite(ftl->write_buffer.lba[group_idx], current_ppn);
                
                current_ppn += 1;
                group_idx++;
                continue;
            }
            
            // 检查步长
            int step = ftl->write_buffer.lba[group_idx + 1] - ftl->write_buffer.lba[group_idx];
            int sequence_end = group_idx;
            
            // 查找具有相同步长的连续序列
            for (int i = group_idx + 1; i <= group_end; i++) {
                if (ftl->write_buffer.lba[i] - ftl->write_buffer.lba[i - 1] == step) {
                    sequence_end = i;
                } else {
                    break;
                }
            }
            
            if (sequence_end > group_idx) {
                // 找到连续序列
                sec.length = (ftl->write_buffer.lba[sequence_end] % SECTORS_PER_GROUP) -
                            (ftl->write_buffer.lba[group_idx] % SECTORS_PER_GROUP);
                sec.step = step;
                
                // 清除连续序列中所有元素的valid位
                for (int i = group_idx; i <= sequence_end; i++) {
                    uint8_t current_offset = ftl->write_buffer.lba[i] % SECTORS_PER_GROUP;
                    int sidx = current_offset / 64;
                    int offsetx = current_offset % 64;
                    
                    if ((ftl->t[current_group].valid[sidx] & (1ULL << offsetx)) != 0) {
                        HashDelete(current_group, current_offset);
                    }
                    ftl->t[current_group].valid[sidx] &= ~(1ULL << offsetx);
                }
                
                Insert(current_group, sec, 0);
                current_ppn += (sequence_end - group_idx) + 1;
                group_idx = sequence_end + 1;
            } else {
                // 单个元素
                int sidx = sec.start / 64;
                int offsetx = sec.start % 64;
                
                // 如果之前有映射，先删除
                if ((ftl->t[current_group].valid[sidx] & (1ULL << offsetx)) != 0) {
                    HashDelete(current_group, sec.start);
                }
                
                ftl->t[current_group].valid[sidx] |= (1ULL << offsetx);
                HashWrite(ftl->write_buffer.lba[group_idx], current_ppn);
                
                current_ppn += 1;
                group_idx++;
            }
        }
        
        idx = group_end + 1;
    }
    
    ftl->write_buffer.next_ppn = current_ppn;
    ftl->write_buffer.count = 0;
}

// 修改FTLRead函数，在读之前检查写缓冲区
uint64_t FTLRead(uint64_t lba) {
    if (!ftl) {
        return 0;
    }
    
    // 检查写缓冲区中是否有这个LBA
    if (is_lba_in_write_buffer(lba)) {
        // 如果有，先处理写缓冲区
        ProcessWriteBuffer();
    }
    
    int idx = lba / SECTORS_PER_GROUP;
    uint8_t offset = lba % SECTORS_PER_GROUP;
    
    if (idx < 0 || idx >= NUMBER_OF_SECTORS) {
        printf("[FTLRead Error] Invalid index: %d for LBA: %lu\n", idx, lba);
        return 0;
    }
    
    // 首先检查哈希表
    int sidx = offset / 64;
    int offsetx = offset % 64;
    if ((ftl->t[idx].valid[sidx] & (1ULL << offsetx)) != 0) {
        return HashRead(lba);
    }
    table *t = &ftl->t[idx];
    
    // 从顶层到底层搜索
    for (int level = 0; level < t->level_count; level++) {
        levelsec *lsec = &t->levels[level];
        
        for (int i = 0; i < lsec->size; i++) {
            section *sec = &lsec->sec[i];
            
            // 跳过无效的section
            if (!is_section_valid(sec)) {
                continue;
            }
            
            // 检查LBA是否在这个段内
                // 精确映射：检查是否在序列中
                if (offset >= sec->start && offset <= sec->start + sec->length) {
                    if (sec->step > 0 && (offset - sec->start) % sec->step == 0) {
                        uint32_t ppa_offset = (offset - sec->start) / sec->step;
                        return sec->b + ppa_offset;
                    }
                }
        }
    }
    
    return 0; // 未找到映射
}

bool FTLModify(uint64_t lba) {
    if (!ftl) {
        return false;
    }
    
    // 添加到写缓冲区
    if (ftl->write_buffer.count < WRITE_BUFFER_SIZE) {
        ftl->write_buffer.lba[ftl->write_buffer.count++] = lba;
        return true;
    } else {
        // 缓冲区满了，先处理再添加
        ProcessWriteBuffer();
        if (ftl->write_buffer.count < WRITE_BUFFER_SIZE) {
            ftl->write_buffer.lba[ftl->write_buffer.count++] = lba;
            return true;
        }
        return false;
    }
}

uint32_t AlgorithmRun(IOVector *ioVector, const char *filename) {
    if (!ioVector) {
        return RETURN_ERROR;
    }
    
    if (!ioVector->ioArray) {
        return RETURN_ERROR;
    }
    
    struct timeval start, end;
    FILE *file = filename ? fopen(filename, "w") : stdout;
    if (!file) {
        printf("[AlgorithmRun Error] Failed to open output file: %s\n", filename);
        return RETURN_ERROR;
    }
    
    // 初始化 FTL
    FTLInit();
    
    // 重置内存统计
    memoryUsed = 0;
    memoryMax = 0;
    // 记录开始时间
    gettimeofday(&start, NULL);
    for (uint64_t i = 0; i < ioVector->len; ++i) {
        if (ioVector->ioArray[i].type == IO_READ) {
            uint64_t ret = FTLRead(ioVector->ioArray[i].lba);
            fprintf(file, "%llu\n", (unsigned long long)ret);
        } else {
            if (!FTLModify(ioVector->ioArray[i].lba)) {
                printf("[AlgorithmRun Error] Failed to modify LBA: %lu\n", ioVector->ioArray[i].lba);
            }
        }
        
        if (memoryUsed > memoryMax) {
            memoryMax = memoryUsed;
        }
    }
    
    // 处理缓冲区中剩余的数据
    ProcessWriteBuffer();
    // 记录结束时间
    gettimeofday(&end, NULL);
    FTLDestroy();
    if (file != stdout) {
        fclose(file);
    }
    
    // 计算秒数和微秒数
    long seconds = end.tv_sec - start.tv_sec;
    long useconds = end.tv_usec - start.tv_usec;
    // 总微秒数
   double during= (seconds * 1000000) + useconds;
    double throughput = (double)ioVector->len / during; // 转换为毫秒
    
    printf("Throughput:\t\t %f IOPS\n", throughput*1000 );
    printf("Max memory used:\t\t %llu B\n", (unsigned long long)memoryMax);
    return RETURN_OK;
}