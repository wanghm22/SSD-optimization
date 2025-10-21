#ifndef FTL_H
#define FTL_H

#include <stdint.h>
#include <stdbool.h>
#include "../public.h"

#ifdef __cplusplus
extern "C" {
#endif

void FTLInit();
void FTLDestroy();
uint64_t FTLRead(uint64_t lba);
bool FTLModify(uint64_t lba);
uint32_t AlgorithmRun(IOVector *ioVector, const char *filename);


#ifdef __cplusplus
}
#endif

#endif  // FTL_H