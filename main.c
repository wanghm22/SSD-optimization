/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "public.h"
#include "ftl/ftl.h"

#define MAX_LINE_LENGTH 256

/* 读取文件内容并解析 */
int ParseFile(const char *filename, IOVector *ioVector)
{
    printf("[DEBUG] Start parsing file: %s\n", filename);
    
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("[Error] Failed to open file");
        printf("[DEBUG] Failed to open file: %s\n", filename);
        return RETURN_ERROR;
    }
    printf("[DEBUG] 文件成功打开\n");

    char line[256];
    int64_t ioCount = 0;
    IOUnit *ioArray = NULL;
    int foundIOCount = 0;  // 标记是否找到io count

    while (fgets(line, sizeof(line), file)) {
        // 去除行尾的换行符
        line[strcspn(line, "\n")] = 0;
        
        
        
        if (strncmp(line, "io count", 8) == 0) {
            printf("[DEBUG] Find 'io count' mark\n");
            if (fgets(line, sizeof(line), file)) {
                line[strcspn(line, "\n")] = 0;
                
                if (sscanf(line, "%u", &ioVector->len) == 1) {
                    ioArray = (IOUnit *)malloc(ioVector->len * sizeof(IOUnit));
                    if (!ioArray) {
                        printf("[Error] malloc error\n");
                        fclose(file);
                        return RETURN_ERROR;
                    }
                    foundIOCount = 1;
                   
                } else {
                    printf("[Error] implement failed\n");
                }
            }
        } else if (strlen(line) > 0) {  // 忽略空行
            if (!foundIOCount) {
                printf("[Error] meet data earlier\n");
                continue;
            }
            
            IOUnit io;
            memset(&io, 0, sizeof(IOUnit));
            
            int parsed = sscanf(line, "%u %llu %llu", &io.type, &io.lba, &io.ppn);
            
            
            if (parsed == 3) {
                if (ioCount < ioVector->len) {
                    ioArray[ioCount] = io;
                    ioCount++;
                    
                } 
                }
            } 
                
            
        }
    
    fclose(file);
    printf("[DEBUG] Over,ioCount = %lld, ioVector->len = %u\n", ioCount, ioVector->len);

    if (!foundIOCount) {
        printf("[Error] No find 'io count' mark\n");
        if (ioArray) free(ioArray);
        return RETURN_ERROR;
    }

    if (ioVector->len != ioCount) {
        printf("[Error] Length(%u)与实际IO数量(%lld)不匹配\n", ioVector->len, ioCount);
        if (ioArray) free(ioArray);
        return RETURN_ERROR;
    }
    
    if (ioVector->len > MAX_IO_NUM) {
        printf("[Error] IO数量(%u)超过最大限制(%u)\n", ioVector->len, MAX_IO_NUM);
        if (ioArray) free(ioArray);
        return RETURN_ERROR;
    }
    
    ioVector->ioArray = ioArray;
    printf("[DEBUG] Succeed,Process %u IO process\n", ioVector->len);
    
    return RETURN_OK;
}
void CompareFiles(const char *filename1, const char *filename2)
{   printf("comparing");
    FILE *file1 = fopen(filename1, "r");
    FILE *file2 = fopen(filename2, "r");
    if (!file1 || !file2) {
        fprintf(stderr, "[Error] Opening files failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    char line1[MAX_LINE_LENGTH], line2[MAX_LINE_LENGTH];
    uint64_t num1, num2;
    unsigned long totalLines = 0;
    unsigned long matchingLines = 0;

    while (1) {
        if (fgets(line1, sizeof(line1), file1)) {
            if (!fgets(line2, sizeof(line2), file2)) {
                fprintf(stderr, "[Error] Output File have different number of lines\n");
                break;
            }
            num1 = strtoull(line1, NULL, 10);
            num2 = strtoull(line2, NULL, 10);
            totalLines++;
            if (num1 == num2) {
                matchingLines++;
            } else {
                printf("Mismatch at line %lu: %lu != %lu\n", totalLines, num1, num2);
            }
        } else {
            if (fgets(line2, sizeof(line2), file2)) {
                fprintf(stderr, "[Error] Output File have different number of lines\n");
            }
            break;
        }
    }

    if (totalLines > 0) {
        double accuracy = (double)matchingLines / totalLines * 100.0;
        printf("Comparison results:\n");
        printf("Total lines: %lu\n", totalLines);
        printf("Matching lines: %lu\n", matchingLines);
        printf("Accuracy: %.2f%%\n", accuracy);
    } else {
        printf("[Error] No lines to compare\n");
    }

    fclose(file1);
    fclose(file2);
}

int main(int argc, char *argv[])
{
    printf("Welcome to HW project.\n");

    /* 输入dataset文件地址 */
    int opt;
    char *inputFile = NULL;
    char *outputFile = NULL;
    char *validateFile = NULL;
    
    // 解析命令行参数
    while ((opt = getopt(argc, argv, "i:o:v:")) != -1) {
        switch (opt) {
            case 'i':
                inputFile = optarg;
                break;
            case 'o':
                outputFile = optarg;
                break;
            case 'v':
                validateFile = optarg;
                break;
            default:
                fprintf(stderr, "Usage: %s -i inputFile -v valFile -o outputFile. [example: ./main -i ./dataset/input_1.txt -o ./dataset/output_1.txt -v ./dataset/val_1.txt] \n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // 检查必要的参数
    if (inputFile == NULL || validateFile == NULL) {
        printf("[Error] 缺少必要的参数:\n");
        if (inputFile == NULL) printf("  - inputFile is NULL\n");
        if (validateFile == NULL) printf("  - validateFile is NULL\n");
        fprintf(stderr, "用法示例: %s -i ./dataset/input_1.txt -o ./dataset/output_1.txt -v ./dataset/val_1.txt\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    printf("Input: %s\n", inputFile);
    printf("Output: %s\n", outputFile ? outputFile : "NULL");
    printf("Validate: %s\n", validateFile);

    // 初始化IOVector
    IOVector *ioVector = (IOVector *)malloc(sizeof(IOVector));
    if (!ioVector) {
        printf("[Error] 分配IOVector内存失败\n");
        return RETURN_ERROR;
    }
    memset(ioVector, 0, sizeof(IOVector));

    // 解析文件
    printf("Start...\n");
    int32_t ret = ParseFile(inputFile, ioVector);
    
    if (ret != RETURN_OK) {
        printf("[Error] fail, 返回值: %d\n", ret);
        free(ioVector);
        return RETURN_ERROR;
    }
    
    printf("succeed,find %u IO process\n", ioVector->len);

    // FTL算法执行
    printf("开始执行FTL算法...\n");
    AlgorithmRun(ioVector, outputFile);
    printf("FTL算法执行完成\n");

    // 验证结果
    if (outputFile) {
        printf("开始验证输出文件...\n");
        CompareFiles(validateFile, outputFile);
    } else {
        printf("[Warning] 未指定输出文件，跳过验证\n");
    }

    // 清理内存
    if (ioVector->ioArray) {
        free(ioVector->ioArray);
    }
    free(ioVector);

    printf("程序执行完成\n");
    return 0;
}