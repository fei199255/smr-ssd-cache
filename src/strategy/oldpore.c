#include <stdlib.h>
#include "pv3.h"
#include "../statusDef.h"
#include "../report.h"
#include "costmodel.h"
#include <math.h>
//#define random(x) (rand()%x)
#define IsDirty(flag) ( (flag & SSD_BUF_DIRTY) != 0 )
#define IsClean(flag) ( (flag & SSD_BUF_DIRTY) == 0 )

#define EVICT_DITRY_GRAIN 64 // The grain of once dirty blocks eviction

typedef struct
{
    long            pagecnt_clean;
    long            head,tail;
    pthread_mutex_t lock;
} CleanDespCtrl;

static blkcnt_t  ZONEBLKSZ;

static Dscptr_paul*   GlobalDespArray;
static ZoneCtrl_pual*            ZoneCtrl_pualArray;
static CleanDespCtrl        CleanCtrl;

static unsigned long*       ZoneSortArray;      /* The zone ID array sorted by weight(calculated customized). it is used to determine the open zones */
static int                  NonEmptyZoneCnt = 0;
static unsigned long*       OpenZoneSet;        /* The decided open zones in current period, which chosed by both the weight-sorted array and the access threshold. */
static int                  OpenZoneCnt;        /* It represent the number of open zones and the first number elements in 'ZoneSortArray' is the open zones ID */

extern long                 Cycle_Length;        /* Which defines the upper limit of the block amount of selected OpenZone and of Evicted blocks. */
static long                 Cycle_Progress;     /* Current times to evict clean/dirty block in a period lenth */
static long                 StampGlobal;      /* Current io sequenced number in a period lenth, used to distinct the degree of heat among zones */
static long                 CycleID;

static void add2ArrayHead(Dscptr_paul* desp, ZoneCtrl_pual* ZoneCtrl_pual);
static void move2ArrayHead(Dscptr_paul* desp,ZoneCtrl_pual* ZoneCtrl_pual);

static int start_new_cycle();
static void stamp(Dscptr_paul * desp);

static void unloadfromZone(Dscptr_paul* desp, ZoneCtrl_pual* ZoneCtrl_pual);
static void clearDesp(Dscptr_paul* desp);
static void hit(Dscptr_paul* desp, ZoneCtrl_pual* ZoneCtrl_pual);
static void add2CleanArrayHead(Dscptr_paul* desp);
static void unloadfromCleanArray(Dscptr_paul* desp);
static void move2CleanArrayHead(Dscptr_paul* desp);

/** PAUL**/
static int redefineOpenZones();
static int get_FrozenOpZone_Seq();
static int random_pick(float weight1, float weight2, float obey);

static volatile unsigned long
getZoneNum(size_t offset)
{
    return offset / ZONESZ;
}

/* Process Function */
int
Init_oldpore()
{
    ZONEBLKSZ = ZONESZ / BLKSZ;

    CycleID = StampGlobal = Cycle_Progress = 0;
    GlobalDespArray = (Dscptr_paul*)malloc(sizeof(Dscptr_paul) * NBLOCK_SSD_CACHE);
    ZoneCtrl_pualArray = (ZoneCtrl_pual*)malloc(sizeof(ZoneCtrl_pual) * NZONES);

    NonEmptyZoneCnt = OpenZoneCnt = 0;
    ZoneSortArray = (unsigned long*)malloc(sizeof(unsigned long) * NZONES);
    OpenZoneSet = (unsigned long*)malloc(sizeof(unsigned long) * NZONES);
    int i = 0;
    while(i < NBLOCK_SSD_CACHE)
    {
        Dscptr_paul* desp = GlobalDespArray + i;
        desp->serial_id = i;
        desp->ssd_buf_tag.offset = -1;
        desp->next = desp->pre = -1;
        desp->heat = 0;
        desp->stamp = 0;
        desp->flag = 0;
        desp->zoneId = -1;
        i++;
    }
    i = 0;
    while(i < NZONES)
    {
        ZoneCtrl_pual* ctrl = ZoneCtrl_pualArray + i;
        ctrl->zoneId = i;
        ctrl->heat = ctrl->pagecnt_clean = ctrl->pagecnt_dirty = 0;
        ctrl->head = ctrl->tail = -1;
        ctrl->score = 0;
        ZoneSortArray[i] = 0;
        i++;
    }
    CleanCtrl.pagecnt_clean = 0;
    CleanCtrl.head = CleanCtrl.tail = -1;
    return 0;
}

int
LogIn_oldpore(long despId, SSDBufTag tag, unsigned flag)
{
    /* activate the decriptor */
    Dscptr_paul* myDesp = GlobalDespArray + despId;
    unsigned long myZoneId = getZoneNum(tag.offset);
    ZoneCtrl_pual* myZone = ZoneCtrl_pualArray + myZoneId;
    myDesp->zoneId = myZoneId;
    myDesp->ssd_buf_tag = tag;
    myDesp->flag |= flag;

    /* add into chain */
    stamp(myDesp);

    if(IsDirty(flag))
    {
        /* add into Zone LRU as it's dirty tag */
        add2ArrayHead(myDesp, myZone);
        myZone->pagecnt_dirty++;
        //myZone->score ++ ;
    }
    else
    {
        /* add into Global Clean LRU as it's clean tag */
        add2CleanArrayHead(myDesp);
        CleanCtrl.pagecnt_clean++;
    }

    return 1;
}

int
Hit_oldpore(long despId, unsigned flag)
{
    Dscptr_paul* myDesp = GlobalDespArray + despId;
    ZoneCtrl_pual* myZone = ZoneCtrl_pualArray + getZoneNum(myDesp->ssd_buf_tag.offset);

    if (IsClean(myDesp->flag) && IsDirty(flag))
    {
        /* clean --> dirty */
        unloadfromCleanArray(myDesp);
        add2ArrayHead(myDesp,myZone);
        myZone->pagecnt_dirty++;
        CleanCtrl.pagecnt_clean--;
        hit(myDesp,myZone);
    }
    else if (IsClean(myDesp->flag) && IsClean(flag))
    {
        /* clean --> clean */
        move2CleanArrayHead(myDesp);
    }
    else
    {
        /* dirty hit again*/
        move2ArrayHead(myDesp,myZone);
        hit(myDesp,myZone);
    }
    stamp(myDesp);
    myDesp->flag |= flag;

    return 0;
}

static int
start_new_cycle()
{
    CycleID++;
    Cycle_Progress = 0;

    int cnt = redefineOpenZones();

    printf("-------------New Cycle!-----------\n");
    printf("Cycle ID [%ld], Non-Empty Zone_Cnt=%d, OpenZones_cnt=%d, CleanBlks=%ld(%0.2lf)\n",CycleID, NonEmptyZoneCnt, OpenZoneCnt,CleanCtrl.pagecnt_clean, (double)CleanCtrl.pagecnt_clean/NBLOCK_SSD_CACHE);

    return cnt;
}

/** \brief
 */
int
LogOut_oldpore(long * out_despid_array, int max_n_batch, enum_t_vict suggest_type)
{
    static int CurEvictZoneSeq;
    static long n_evict_clean_cycle = 0, n_evict_dirty_cycle = 0;
    int evict_cnt = 0;

    ZoneCtrl_pual* evictZone;

    if(suggest_type == ENUM_B_Clean)
    {
        if(CleanCtrl.pagecnt_clean == 0) // Consistency judgment
            usr_error("Order to evict clean cache block, but it is exhausted in advance.");
        goto FLAG_EVICT_CLEAN;
    }
    else if(suggest_type == ENUM_B_Dirty)
    {
        if(STT->incache_n_dirty == 0)   // Consistency judgment
            usr_error("Order to evict dirty cache block, but it is exhausted in advance.");
        goto FLAG_EVICT_DIRTYZONE;
    }
    else if(suggest_type == ENUM_B_Any)
    {
        if(STT->incache_n_clean == 0)
            goto FLAG_EVICT_DIRTYZONE;
        else if(STT->incache_n_dirty == 0)
            goto FLAG_EVICT_CLEAN;
        else
        {
            int it = random_pick((float)STT->incache_n_clean, (float)STT->incache_n_dirty, 1);
            if(it == 1)
                goto FLAG_EVICT_CLEAN;
            else
                goto FLAG_EVICT_DIRTYZONE;
        }
    }
    else
        usr_error("PAUL doesn't support this evict type.");

FLAG_EVICT_CLEAN:
    while(evict_cnt < EVICT_DITRY_GRAIN && CleanCtrl.pagecnt_clean > 0)
    {
        Dscptr_paul * cleanDesp = GlobalDespArray + CleanCtrl.tail;
        out_despid_array[evict_cnt] = cleanDesp->serial_id;
        unloadfromCleanArray(cleanDesp);
        clearDesp(cleanDesp);

        n_evict_clean_cycle ++;
        CleanCtrl.pagecnt_clean --;
        evict_cnt ++;
    }
    return evict_cnt;

FLAG_EVICT_DIRTYZONE:
    CurEvictZoneSeq = get_FrozenOpZone_Seq();
    if(CurEvictZoneSeq < 0 || Cycle_Progress == 0){
        start_new_cycle();

        printf("Ouput of last Cycle: clean:%ld, dirty:%ld\n",n_evict_clean_cycle,n_evict_dirty_cycle);
        n_evict_clean_cycle = n_evict_dirty_cycle = 0;

        if((CurEvictZoneSeq = get_FrozenOpZone_Seq()) < 0)
            usr_error("FLAG_EVICT_DIRTYZONE error");
    }

    evictZone = ZoneCtrl_pualArray + OpenZoneSet[CurEvictZoneSeq];

    while(evict_cnt < EVICT_DITRY_GRAIN && evictZone->pagecnt_dirty > 0)
    {
        Dscptr_paul* frozenDesp = GlobalDespArray + evictZone->tail;

        unloadfromZone(frozenDesp,evictZone);
        out_despid_array[evict_cnt] = frozenDesp->serial_id;
//        evictZone->score -= (double) 1 / (1 << frozenDesp->heat);

        Cycle_Progress ++;
        evictZone->pagecnt_dirty--;
        evictZone->heat -= frozenDesp->heat;
        n_evict_dirty_cycle++;

        clearDesp(frozenDesp);
        evict_cnt++;
    }
    //printf("pore+V2: batch flush dirty cnt [%d] from zone[%lu]\n", j,evictZone->zoneId);

//    printf("SCORE REPORT: zone id[%d], score[%lu]\n", evictZone->zoneId, evictZone->score);
    return evict_cnt;
}

/****************
** Utilities ****
*****************/
/* Utilities for Dirty descriptors Array in each Zone*/

static void
hit(Dscptr_paul* desp, ZoneCtrl_pual* ZoneCtrl_pual)
{
    desp->heat ++;
    ZoneCtrl_pual->heat++;
//    ZoneCtrl_pual->score -= (double) 1 / (1 << desp->heat);
}

static void
add2ArrayHead(Dscptr_paul* desp, ZoneCtrl_pual* ZoneCtrl_pual)
{
    if(ZoneCtrl_pual->head < 0)
    {
        //empty
        ZoneCtrl_pual->head = ZoneCtrl_pual->tail = desp->serial_id;
    }
    else
    {
        //unempty
        Dscptr_paul* headDesp = GlobalDespArray + ZoneCtrl_pual->head;
        desp->pre = -1;
        desp->next = ZoneCtrl_pual->head;
        headDesp->pre = desp->serial_id;
        ZoneCtrl_pual->head = desp->serial_id;
    }
}

static void
unloadfromZone(Dscptr_paul* desp, ZoneCtrl_pual* ZoneCtrl_pual)
{
    if(desp->pre < 0)
    {
        ZoneCtrl_pual->head = desp->next;
    }
    else
    {
        GlobalDespArray[desp->pre].next = desp->next;
    }

    if(desp->next < 0)
    {
        ZoneCtrl_pual->tail = desp->pre;
    }
    else
    {
        GlobalDespArray[desp->next].pre = desp->pre;
    }
    desp->pre = desp->next = -1;
}

static void
move2ArrayHead(Dscptr_paul* desp,ZoneCtrl_pual* ZoneCtrl_pual)
{
    unloadfromZone(desp, ZoneCtrl_pual);
    add2ArrayHead(desp, ZoneCtrl_pual);
}

static void
clearDesp(Dscptr_paul* desp)
{
    desp->ssd_buf_tag.offset = -1;
    desp->next = desp->pre = -1;
    desp->heat = 0;
    desp->stamp = 0;
    desp->flag &= ~(SSD_BUF_DIRTY | SSD_BUF_VALID);
    desp->zoneId = -1;
}

/* Utilities for Global Clean Descriptors Array */
static void
add2CleanArrayHead(Dscptr_paul* desp)
{
    if(CleanCtrl.head < 0)
    {
        //empty
        CleanCtrl.head = CleanCtrl.tail = desp->serial_id;
    }
    else
    {
        //unempty
        Dscptr_paul* headDesp = GlobalDespArray + CleanCtrl.head;
        desp->pre = -1;
        desp->next = CleanCtrl.head;
        headDesp->pre = desp->serial_id;
        CleanCtrl.head = desp->serial_id;
    }
}

static void
unloadfromCleanArray(Dscptr_paul* desp)
{
    if(desp->pre < 0)
    {
        CleanCtrl.head = desp->next;
    }
    else
    {
        GlobalDespArray[desp->pre].next = desp->next;
    }

    if(desp->next < 0)
    {
        CleanCtrl.tail = desp->pre;
    }
    else
    {
        GlobalDespArray[desp->next].pre = desp->pre;
    }
    desp->pre = desp->next = -1;
}

static void
move2CleanArrayHead(Dscptr_paul* desp)
{
    unloadfromCleanArray(desp);
    add2CleanArrayHead(desp);
}

/* Decision Method */
/** \brief
 *  Quick-Sort method to sort the zones by score.
    NOTICE!
        If the gap between variable 'start' and 'end', it will PROBABLY cause call stack OVERFLOW!
        So this function need to modify for better.
 */
static void
qsort_zone(long start, long end)
{
    long		i = start;
    long		j = end;

    long S = ZoneSortArray[start];
    ZoneCtrl_pual* curCtrl = ZoneCtrl_pualArray + S;
    unsigned long sScore = curCtrl->score;
    while (i < j)
    {
        while (!(ZoneCtrl_pualArray[ZoneSortArray[j]].score > sScore) && i<j)
        {
            j--;
        }
        ZoneSortArray[i] = ZoneSortArray[j];

        while (!(ZoneCtrl_pualArray[ZoneSortArray[i]].score < sScore) && i<j)
        {
            i++;
        }
        ZoneSortArray[j] = ZoneSortArray[i];
    }

    ZoneSortArray[i] = S;
    if (i - 1 > start)
        qsort_zone(start, i - 1);
    if (j + 1 < end)
        qsort_zone(j + 1, end);
}

static long
extractNonEmptyZoneId()
{
    int zoneId = 0, cnt = 0;
    while(zoneId < NZONES)
    {
        ZoneCtrl_pual* zone = ZoneCtrl_pualArray + zoneId;
        if(zone->pagecnt_dirty > 0)
        {
            ZoneSortArray[cnt] = zoneId;
            cnt++;
        }
        zoneId++;
    }
    return cnt;
}

static void
pause_and_score()
{
    /*  For simplicity, searching all the zones of SMR,
        actually it's only needed to search the zones which had been cached.
        But it doesn't matter because of only 200~500K meta data of zones in memory for searching, it's not a big number.
    */
    /* Score all zones. */
    blkcnt_t n = 0;
    ZoneCtrl_pual * myCtrl;
    while(n < NonEmptyZoneCnt)
    {
        myCtrl = ZoneCtrl_pualArray + ZoneSortArray[n];
        myCtrl->score = 0;
        myCtrl->score = myCtrl->pagecnt_dirty / (myCtrl->heat+1);
        n++ ;
    }
}


static int
redefineOpenZones()
{
    NonEmptyZoneCnt = extractNonEmptyZoneId();
    if(NonEmptyZoneCnt == 0)
        return 0;
    pause_and_score(); /** ARS (Actually Release Space) */
    qsort_zone(0,NonEmptyZoneCnt-1);

    OpenZoneCnt = 0;
    long i = 0, n_chooseblk = 0;
    while(i < NonEmptyZoneCnt)
    {
        ZoneCtrl_pual* zone = ZoneCtrl_pualArray + ZoneSortArray[i];

        if(n_chooseblk + zone->pagecnt_dirty > Cycle_Length)
            break;

        n_chooseblk += zone->pagecnt_dirty;
        OpenZoneSet[OpenZoneCnt] = zone->zoneId;
        OpenZoneCnt++;
        i++;
    }
    return OpenZoneCnt;
}

static int
get_FrozenOpZone_Seq()
{
    int seq = 0;
    blkcnt_t frozenSeq = -1;
    long frozenStamp = CycleID;
    while(seq < OpenZoneCnt)
    {
        ZoneCtrl_pual* ctrl = ZoneCtrl_pualArray + OpenZoneSet[seq];
        if(ctrl->pagecnt_dirty <= 0)
        {
            seq ++;
            continue;
        }

        Dscptr_paul* tail = GlobalDespArray + ctrl->tail;
        if(tail->stamp < frozenStamp)
        {
            frozenStamp = tail->stamp;
            frozenSeq = seq;
        }
        seq ++;
    }

    return frozenSeq;   // If return value <= 0, it means 1. here already has no any dirty block in the selected bands. 2. here has not started the cycle.
}

static void stamp(Dscptr_paul * desp)
{
    desp->stamp = CycleID;
}

static int random_pick(float weight1, float weight2, float obey)
{
    //return (weight1 < weight2) ? 1 : 2;
    // let weight as the standard, set as 1,
    float inc_times = (weight2 / weight1) - 1;
    inc_times *= obey;

    float de_point = 1000 * (1 / (2 + inc_times));
//    rand_init();
    int token = rand() % 1000;

    if(token < de_point)
        return 1;
    return 2;
}