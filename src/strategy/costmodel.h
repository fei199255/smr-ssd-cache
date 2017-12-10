#ifndef COSTMODEL_H
#define COSTMODEL_H
#include <unistd.h>
#include <stdlib.h>

#include "global.h"
#include "cache.h"
#include "timerUtils.h"
#include "report.h"
typedef struct cm_token
{
	int will_evict_dirty_blkcnt;
	int will_evict_clean_blkcnt;
	double wtramp;
}cm_token;

extern int CM_Init(blkcnt_t windowSize);
extern int CM_Reg_EvictBlk(SSDBufTag blktag, unsigned flag, microsecond_t usetime);
extern int CM_TryCallBack(SSDBufTag blktag);

extern int CM_T_rand_Reg(microsecond_t usetime);
extern int CM_T_hitmiss_Reg(microsecond_t usetime);
/** Calling for Strategy **/
extern int CM_CHOOSE(cm_token token);
extern void ReportCM();
#endif // COSTMODEL_H

