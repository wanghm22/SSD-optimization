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

static uint64_t memoryUsed = 0;
static uint64_t memoryMax = 0;

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
    bool accuracy;  
    // 移除了valid字段
} section;

typedef struct {
    section *sec;
    uint8_t size;
    uint8_t capacity;
} levelsec;

typedef struct {
    levelsec *levels;
    uint8_t level_count;
} table;

typedef struct {
    table t[NUMBER_OF_SECTORS];
    WriteBuffer write_buffer;
} FTL;

static FTL *ftl = NULL;

void FTLInit() {
    ftl = calloc(1, sizeof(FTL));
    if (!ftl) {
        return;
    }
    
    memoryUsed += sizeof(FTL);
    ftl->write_buffer.next_ppn = 1000;
}

void FTLDestroy() {
    if (!ftl) return;
    
    for (int i = 0; i < NUMBER_OF_SECTORS; i++) {
        for (int j = 0; j < ftl->t[i].level_count; j++) {
            if (ftl->t[i].levels[j].sec) {
                free(ftl->t[i].levels[j].sec);
            }
        }
        if (ftl->t[i].levels) {
            free(ftl->t[i].levels);
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
    // 如果任意一个section无效，则不重叠
    if (!is_section_valid(a) || !is_section_valid(b)) {
        return false;
    }
    
    // 处理单个元素的情况
    uint8_t a_end = a->accuracy ? (a->start + a->length) : a->start;
    uint8_t b_end = b->accuracy ? (b->start + b->length) : b->start;
    
    // 对于单个元素，我们只检查精确匹配
    if (!a->accuracy && !b->accuracy) {
        return a->start == b->start;
    }
    
    // 对于连续序列，检查范围重叠
    return !(a->start > b_end || a_end < b->start);
}

// 简化的Insert函数 - 使用无效化而不是内存重新分配
void Insert(int idx, section new_sec, int start_level) {
    memoryUsed += sizeof(section);
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
            ftl->t[idx].levels[ftl->t[idx].level_count].capacity = 0;
            ftl->t[idx].level_count = new_count;
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
            if (current_level_ptr->size >= current_level_ptr->capacity) {
                uint8_t new_capacity = current_level_ptr->capacity == 0 ? 4 : current_level_ptr->capacity * 2;
                section *new_secs = realloc(current_level_ptr->sec, new_capacity * sizeof(section));
                if (!new_secs) return;
                current_level_ptr->sec = new_secs;
                current_level_ptr->capacity = new_capacity;
            }
            current_level_ptr->sec[current_level_ptr->size++] = current_sec;
            
            // 将冲突的section作为下一轮要处理的section
            current_sec = temp_sec;
            current_level++;
        } else {
            // 没有冲突，直接插入当前层
            if (current_level_ptr->size >= current_level_ptr->capacity) {
                uint8_t new_capacity = current_level_ptr->capacity == 0 ? 4 : current_level_ptr->capacity * 2;
                section *new_secs = realloc(current_level_ptr->sec, new_capacity * sizeof(section));
                if (!new_secs) return;
                current_level_ptr->sec = new_secs;
                current_level_ptr->capacity = new_capacity;
            }
            current_level_ptr->sec[current_level_ptr->size++] = current_sec;
            break;
        }
    }
}

// 修改FTLRead函数，跳过无效的section
uint64_t FTLRead(uint64_t lba) {
    if (!ftl) {
        return 0;
    }
    
    int idx = lba / SECTORS_PER_GROUP;
    uint8_t offset = lba % SECTORS_PER_GROUP;
    
    if (idx < 0 || idx >= NUMBER_OF_SECTORS) {
        printf("[FTLRead Error] Invalid index: %d for LBA: %lu\n", idx, lba);
        return 0;
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
            if (offset >= sec->start && offset <= sec->start + sec->length) {
                if (sec->accuracy) {
                    // 精确映射：使用步长计算
                    if (sec->step > 0) {
                        // 检查是否在步长点上
                        if ((offset - sec->start) % sec->step == 0) {
                            uint32_t ppa_offset = (offset - sec->start) / sec->step;
                            uint64_t result = sec->b + ppa_offset * FLASH_PAGE_SIZE;
                            return result;
                        }
                    }
                } else {
                    // 近似段（单个点）：直接匹配start值
                    if (offset == sec->start) {
                        return sec->b;
                    }
                }
                break; // 在这个段中但没找到匹配，跳出内层循环
            }
        }
    }
    
    return 0; // 未找到映射
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
            sec.b = current_ppn * FLASH_PAGE_SIZE;
            // 不再设置valid字段
            
            // 检查是否是单个元素
            if (group_idx == group_end) {
                sec.length = 0;
                sec.step = 0;
                sec.accuracy = 0;
                Insert(current_group, sec, 0);
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
                sec.accuracy = 1;
                Insert(current_group, sec, 0);
                current_ppn += (sequence_end - group_idx) + 1;
                group_idx = sequence_end + 1;
            } else {
                // 单个元素
                sec.length = 0;
                sec.step = 0;
                sec.accuracy = 0;
                Insert(current_group, sec, 0);
                current_ppn += 1;
                group_idx++;
            }
        }
        
        idx = group_end + 1;
    }
    
    ftl->write_buffer.next_ppn = current_ppn;
    ftl->write_buffer.count = 0;
}

// FTLModify和AlgorithmRun函数保持不变
bool FTLModify(uint64_t lba) {
    if (!ftl) {
        return false;
    }
    
    // 添加到写缓冲区
    if (ftl->write_buffer.count < WRITE_BUFFER_SIZE) {
        ftl->write_buffer.lba[ftl->write_buffer.count++] = lba;
    } else {
        return false;
    }
    
    // 如果缓冲区满了，处理缓冲区
    if (ftl->write_buffer.count >= WRITE_BUFFER_SIZE) {
        ProcessWriteBuffer();
    }
    
    return true;
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
    double during = (seconds * 1000000.0 + useconds) / 1000.0;  // 转换为毫秒
    printf("algorithmRunningDuration:\t %f ms\n", during);
    printf("Max memory used:\t\t %llu B\n", (unsigned long long)memoryMax);

    return RETURN_OK;
}