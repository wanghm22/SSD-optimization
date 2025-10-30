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
#define tolerance 2
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
    uint8_t *data;          // 扁平数组存储所有LBA，用CRB_SEPARATOR分隔段
    int size;               // data数组当前大小
    uint32_t *segments_ppn; // 每个段的起始PPN数组
} CRB;

typedef struct {
    uint8_t start;
    uint8_t length;
    uint8_t step;
    uint32_t b; 
} section;

typedef struct {
    section *sec;
    uint8_t size;
    uint8_t capacity;
} levelsec;

typedef struct {
    levelsec *levels;
    uint8_t level_count;
    CRB crb;
    
    uint64_t bitmap[4];  // 修改：4个uint64_t，共256位，表示256个LBA的状态
} table;

typedef struct {
    table t[NUMBER_OF_SECTORS];
    WriteBuffer write_buffer;
} FTL;

static FTL *ftl = NULL;

// 定义分隔符
#define CRB_SEPARATOR 0xFF

// 设置bitmap中某一位的状态
void set_bitmap_status(table *t, uint8_t lba_offset, bool in_section) {
    if (lba_offset >= 256) return;
    
    int bitmap_index = lba_offset / 64;
    int bit_position = lba_offset % 64;
    
    if (in_section) {
        // 设置为1，表示在section中
        t->bitmap[bitmap_index] |= (1ULL << bit_position);
    } else {
        // 设置为0，表示在CRB中
        t->bitmap[bitmap_index] &= ~(1ULL << bit_position);
    }
}

// 获取bitmap中某一位的状态
bool get_bitmap_status(table *t, uint8_t lba_offset) {
    if (lba_offset >= 256) return false;
    
    int bitmap_index = lba_offset / 64;
    int bit_position = lba_offset % 64;
    
    // 返回true表示在section中，false表示在CRB中
    return (t->bitmap[bitmap_index] >> bit_position) & 1;
}

// 初始化CRB
void init_crb(CRB *crb) {
    crb->data = NULL;
    crb->size = 0;
    crb->segments_ppn = NULL;
}

// 释放CRB内存
void free_crb(CRB *crb) {
    if (crb->data) {
        free(crb->data);
        crb->data = NULL;
    }
    if (crb->segments_ppn) {
        free(crb->segments_ppn);
        crb->segments_ppn = NULL;
    }
    crb->size = 0;
}

// 在CRB中查找指定LBA对应的PBA
uint64_t crb_search(CRB *crb, uint8_t lba_offset) {
    if (!crb || !crb->data || crb->size == 0 || !crb->segments_ppn) {
        return 0;
    }
    
    int current_segment = 0;
    int segment_start_index = 0;
    int segment_ppn_index = 0;
    
    // 遍历整个data数组
    for (int i = 0; i < crb->size; i++) {
        if (crb->data[i] == CRB_SEPARATOR) {
            // 遇到分隔符，移动到下一个段
            current_segment++;
            segment_start_index = i + 1;
            segment_ppn_index++;
            continue;
        }
        
        // 检查当前LBA是否匹配
        if (crb->data[i] == lba_offset) {
            // 计算在当前段中的位置
            int position_in_segment = i - segment_start_index;
            
            // 返回对应的PBA：段起始PPN + 在段中的位置 * FLASH_PAGE_SIZE
            return crb->segments_ppn[current_segment] + position_in_segment * FLASH_PAGE_SIZE;
        }
        
        // 如果当前LBA大于要找的LBA，且后面是分隔符，说明不存在
        if (crb->data[i] > lba_offset && 
            i + 1 < crb->size && 
            crb->data[i + 1] == CRB_SEPARATOR) {
            break;
        }
    }
    
    return 0; // 未找到
}

// 计算段的数量
int count_segments(CRB *crb) {
    if (!crb || !crb->data || crb->size == 0) {
        return 0;
    }
    
    int count = 1; // 至少有一个段
    for (int i = 0; i < crb->size; i++) {
        if (crb->data[i] == CRB_SEPARATOR) {
            count++;
        }
    }
    return count;
}

// CRB插入函数
void crbinsert(int group, int *lba_offsets, int size, uint32_t segment_ppn) {
    if (!ftl || group < 0 || group >= NUMBER_OF_SECTORS || !lba_offsets || size <= 0) {
        return;
    }
    
    CRB *crb = &ftl->t[group].crb;
    
    // 如果CRB未初始化，先初始化data数组
    if (crb->data == NULL) {
        crb->data = malloc(size * sizeof(uint8_t));
        if (!crb->data) return;
        crb->size = 0;
    }
    
    // 对输入的LBA偏移量进行排序和去重
    uint8_t *sorted_lbas = malloc(size * sizeof(uint8_t));
    int sorted_size = 0;
    
    // 简单的插入排序和去重
    for (int i = 0; i < size; i++) {
        uint8_t current = (uint8_t)lba_offsets[i];
        int pos = 0;
        while (pos < sorted_size && sorted_lbas[pos] < current) {
            pos++;
        }
        
        if (pos < sorted_size && sorted_lbas[pos] == current) {
            continue; // 去重
        }
        
        // 插入到正确位置
        for (int j = sorted_size; j > pos; j--) {
            sorted_lbas[j] = sorted_lbas[j - 1];
        }
        sorted_lbas[pos] = current;
        sorted_size++;
    }
    
    // 计算需要的新空间：当前size + 新段大小 + 1个分隔符（如果不是第一个段）
    int current_segment_count = count_segments(crb);
    int needs_separator = (crb->size > 0) ? 1 : 0;
    int new_total_size = crb->size + sorted_size + needs_separator;
    
    // 重新分配data数组
    uint8_t *new_data = realloc(crb->data, new_total_size * sizeof(uint8_t));
    if (!new_data) {
        free(sorted_lbas);
        return;
    }
    crb->data = new_data;
    
    // 重新分配segments_ppn数组
    int new_segment_count = current_segment_count + 1;
    uint32_t *new_ppn = realloc(crb->segments_ppn, new_segment_count * sizeof(uint32_t));
    if (!new_ppn) {
        free(sorted_lbas);
        return;
    }
    crb->segments_ppn = new_ppn;
    
    // 如果是第一个段，直接复制数据
    if (current_segment_count == 0) {
        for (int i = 0; i < sorted_size; i++) {
            crb->data[i] = sorted_lbas[i];
        }
        crb->segments_ppn[0] = segment_ppn;
        crb->size = sorted_size;
    } else {
        // 不是第一个段，需要找到插入位置
        
        // 首先找到新段应该插入的位置（按第一个LBA的大小排序）
        int insert_position = crb->size;
        int target_segment_index = current_segment_count;
        
        for (int i = 0; i < crb->size; i++) {
            if (crb->data[i] == CRB_SEPARATOR) {
                continue;
            }
            
            // 找到第一个大于新段第一个LBA的位置
            if (crb->data[i] > sorted_lbas[0]) {
                // 回溯找到这个段的开始位置
                insert_position = i;
                while (insert_position > 0 && crb->data[insert_position - 1] != CRB_SEPARATOR) {
                    insert_position--;
                }
                
                // 计算目标段索引
                target_segment_index = 0;
                for (int j = 0; j < insert_position; j++) {
                    if (crb->data[j] == CRB_SEPARATOR) {
                        target_segment_index++;
                    }
                }
                break;
            }
        }
        
        // 移动现有数据为新段腾出空间
        for (int i = crb->size - 1; i >= insert_position; i--) {
            crb->data[i + sorted_size + 1] = crb->data[i];
        }
        
        // 移动segments_ppn数组
        for (int i = current_segment_count - 1; i >= target_segment_index; i--) {
            crb->segments_ppn[i + 1] = crb->segments_ppn[i];
        }
        
        // 插入分隔符和新段数据
        if (insert_position < crb->size) {
            crb->data[insert_position] = CRB_SEPARATOR;
        }
        
        for (int i = 0; i < sorted_size; i++) {
            crb->data[insert_position + 1 + i] = sorted_lbas[i];
        }
        
        // 设置新段的PPN
        crb->segments_ppn[target_segment_index] = segment_ppn;
        crb->size = new_total_size;
    }
    
    free(sorted_lbas);
    
    // 更新内存使用统计
    memoryUsed += (sorted_size * sizeof(uint8_t) + sizeof(uint32_t));
}

void FTLInit() {
    ftl = calloc(1, sizeof(FTL));
    if (!ftl) {
        return;
    }
    
    memoryUsed += sizeof(FTL);
    ftl->write_buffer.next_ppn = 1000;
    for(int i = 0; i < NUMBER_OF_SECTORS; i++){
        
        init_crb(&ftl->t[i].crb);
        // 初始化bitmap，所有bit为0，表示都在CRB中
        for (int j = 0; j < 4; j++) {
            ftl->t[i].bitmap[j] = 0;
        }
    }
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
        free_crb(&ftl->t[i].crb);
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
    uint8_t a_end = (a->length > 0) ? (a->start + a->length) : a->start;
    uint8_t b_end = (b->length > 0) ? (b->start + b->length) : b->start;
    
    // 对于单个元素，我们只检查精确匹配
    if (a->length == 0 && b->length == 0) {
        return a->start == b->start;
    }
    
    // 对于连续序列，检查范围重叠
    return !(a->start > b_end || a_end < b->start);
}

// 从levelsec中删除指定索引的section，并压缩数组
void remove_section_from_level(levelsec *lsec, int index) {
    if (!lsec || index < 0 || index >= lsec->size) {
        return;
    }
    
    // 将后面的元素前移
    for (int i = index; i < lsec->size - 1; i++) {
        lsec->sec[i] = lsec->sec[i + 1];
    }
    lsec->size--;
    
    // 如果数组变得太小，可以缩小容量以节省内存
    if (lsec->capacity > 4 && lsec->size < lsec->capacity / 2) {
        uint8_t new_capacity = lsec->capacity / 2;
        section *new_secs = realloc(lsec->sec, new_capacity * sizeof(section));
        if (new_secs) {
            lsec->sec = new_secs;
            lsec->capacity = new_capacity;
            memoryUsed -= (lsec->capacity - new_capacity) * sizeof(section);
        }
    }
}

// 修改后的Insert函数 - 彻底删除重叠的section而不是仅仅无效化
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
            
            // 彻底删除当前层的冲突section
            remove_section_from_level(current_level_ptr, conflict_index);
            memoryUsed -= sizeof(section);
            
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
            
            // 更新bitmap状态：将这个section覆盖的所有LBA标记为在section中
            if (current_sec.length > 0) {
                // 连续序列
                for (uint8_t offset = current_sec.start; offset <= current_sec.start + current_sec.length; offset += (current_sec.step > 0 ? current_sec.step : 1)) {
                    set_bitmap_status(&ftl->t[idx], offset, true);
                }
            } else {
                // 单个元素
                set_bitmap_status(&ftl->t[idx], current_sec.start, true);
            }
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
            int* data = NULL;
            int size = 0;
            section sec;
            sec.start = ftl->write_buffer.lba[group_idx] % SECTORS_PER_GROUP;
            sec.b = current_ppn * FLASH_PAGE_SIZE;
            
            uint64_t pba = current_ppn * FLASH_PAGE_SIZE;
            
            // 检查是否是单个元素
            if (group_idx == group_end) {
                
                data = malloc(sizeof(int));
                data[0] = ftl->write_buffer.lba[group_idx] % SECTORS_PER_GROUP;
                size = 1;
                crbinsert(current_group, data, size, current_ppn);
                // 更新bitmap状态：新插入CRB的LBA标记为在CRB中
                set_bitmap_status(&ftl->t[current_group], data[0], false);
                current_ppn++;
                group_idx++;
                free(data);
                continue;
            }
            
            data = malloc(sizeof(int));
            data[0] = ftl->write_buffer.lba[group_idx] % SECTORS_PER_GROUP;
            size = 1;
            
            // 检查步长
            int step = ftl->write_buffer.lba[group_idx + 1] - ftl->write_buffer.lba[group_idx];
            int sequence_end = group_idx;
            bool is_continuous = true;
            
            // 查找具有相同步长的连续序列
            for (int i = group_idx + 1; i <= group_end; i++) {
                if ((ftl->write_buffer.lba[i] - ftl->write_buffer.lba[i - 1] >= tolerance + step) && 
                    (ftl->write_buffer.lba[i] - ftl->write_buffer.lba[i - 1] <= tolerance + step)) {
                    size++;
                    int *data_ = (int*)realloc(data, sizeof(int) * size);
                    data = data_;
                    data[size - 1] = ftl->write_buffer.lba[i] % SECTORS_PER_GROUP;
                    
                    if (ftl->write_buffer.lba[i] - ftl->write_buffer.lba[i - 1] == step) {
                        sequence_end = i;
                    } else {
                        is_continuous = false;
                    }
                } else {
                    break;
                }
            }
            
            if (sequence_end > group_idx && is_continuous) {
                // 找到连续序列
                sec.length = (ftl->write_buffer.lba[sequence_end] % SECTORS_PER_GROUP) - 
                            (ftl->write_buffer.lba[group_idx] % SECTORS_PER_GROUP);
                sec.step = step;
                
                Insert(current_group, sec, 0);
                current_ppn += (sequence_end - group_idx) + 1;
                group_idx = sequence_end + 1;
            } else {
                // 单个元素或非连续序列
              
                crbinsert(current_group, data, size, current_ppn);
                // 更新bitmap状态：新插入CRB的LBA标记为在CRB中
                for (int i = 0; i < size; i++) {
                    set_bitmap_status(&ftl->t[current_group], data[i], false);
                }
                current_ppn += size;
                group_idx += size;
            }
            
            free(data);
        }
        
        idx = group_end + 1;
    }
    
    ftl->write_buffer.next_ppn = current_ppn;
    ftl->write_buffer.count = 0;
}

// 在section中查找LBA
uint64_t search_in_sections(table *t, uint8_t offset) {
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
            if (sec->length > 0) {
                // 连续序列的情况
                if (offset >= sec->start && offset <= sec->start + sec->length) {
                    if (sec->step > 0 && (offset - sec->start) % sec->step == 0) {
                        uint32_t ppa_offset = (offset - sec->start) / sec->step;
                        uint64_t result = sec->b + ppa_offset * FLASH_PAGE_SIZE;
                        return result;
                    }
                }
            } else {
                // 单个元素的情况
                if (offset == sec->start) {
                    return sec->b;
                }
            }
        }
    }
    return 0;
}

// 修改FTLRead函数，根据bitmap决定查找位置
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
    
    table *t = &ftl->t[idx];
    
    // 1. 首先在红黑树中查找
    
    
    // 2. 根据bitmap状态决定查找位置
    bool in_section = get_bitmap_status(t, offset);
    
    if (in_section) {
        // 在section中查找
        return search_in_sections(t, offset);
    } else {
        // 在CRB中查找
        return crb_search(&t->crb, offset);
    }
}

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
    double memory=(double)memoryMax/(1024.0*1024.0);
    double during = (seconds * 1000000.0 + useconds) / 1000.0;  // 转换为毫秒
    double throughput = ioVector->len / during;  // 计算吞吐量
    printf("algorithmRunningDuration:\t %f ms\n", throughput);
    printf("Max memory used:\t\t %f MB\n", memory);

    return RETURN_OK;
}