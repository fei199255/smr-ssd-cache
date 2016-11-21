#ifndef BANDTABLE_H
#define BANDTABLE_H

#define DEBUG 0
/*-----------------------------------band----------------------------*/
#define bool unsigned char
#define size_t long
typedef struct BandHashBucket
{
	long band_id;
	long ssd_buf_band_id;
	struct BandHashBucket *next_item;
} BandHashBucket;

BandHashBucket *band_hashtable;
#define GetBandHashBucket(hash_code) ((BandHashBucket *)(band_hashtable +(unsigned)(hash_code)))

extern unsigned long NBANDTables;
extern unsigned long NSMRBands;

extern void initBandTable(size_t size);
extern unsigned long bandtableHashcode(long band_id);
extern long bandtableLookup(long band_id,unsigned long hash_code);
extern long bandtableInsert(long band_id,unsigned long hash_code,long first_page);
extern long bandtableDelete(long band_id,unsigned long hasd_code);
#endif    /*  BANDTABLE_H*/