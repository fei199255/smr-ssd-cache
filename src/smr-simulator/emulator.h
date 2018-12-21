#ifndef SMR_SSD_CACHE_SMR_SIMULATOR_H
#define SMR_SSD_CACHE_SMR_SIMULATOR_H

#include "global.h"
#include "statusDef.h"

#define DEBUG 0
/* ---------------------------smr simulator---------------------------- */
#include <pthread.h>

typedef struct
{
    off_t offset;
} DespTag;

typedef struct
{
    DespTag tag;
    long    despId;
    int     isValid;
} FIFODesc;

typedef struct
{
	unsigned long	n_used;
             long   head, tail;
} FIFOCtrl;

extern int  fd_fifo_part;
extern int  fd_smr_part;
extern void InitEmulator();
extern int emu_smr_read(char *buffer, size_t size, off_t offset);
extern int emu_smr_write(char *buffer, size_t size, off_t offset);
extern void PrintSimulatorStatistic();
extern void CloseSMREmu();
#endif
