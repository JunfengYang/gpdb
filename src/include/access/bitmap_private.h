/*-------------------------------------------------------------------------
 *
 * bitmap_private.h
 *	  Bitmap index internal definitions.
 *
 *
 * Portions Copyright (c) 2012-Present VMware, Inc. or its affiliates.
 * Portions Copyright (c) 1996-2019, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/access/bitmap_private.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef BITMAP_PRIVATE_H
#define BITMAP_PRIVATE_H

#include "access/bitmap.h"
#include "access/genam.h"
#include "access/htup.h"
#include "utils/hsearch.h"

/* This file can not and should not depend on execnodes.h */
struct IndexInfo;

/*
 * Data structure for used to buffer index creation during bmbuild().
 * Buffering provides three benefits: firstly, it makes for many fewer
 * calls to the lower-level bitmap insert functions; secondly, it means that
 * we reduce the amount of unnecessary compression and decompression we do;
 * thirdly, in some cases pages for a given bitmap vector will be contiguous
 * on disk.
 *
 * byte_size counts how many bytes we've consumed in the buffer.
 * max_lov_block is a hint as to whether we'll find a LOV block in lov_blocks
 * or not (we take advantage of the fact that LOV block numbers will be
 * increasing).
 * lov_blocks is a list of LOV block buffers. The structures put in
 * this list are defined in bitmapinsert.c.
 */
typedef struct BMTidBuildBuf
{
	uint32 byte_size; /* The size in bytes of the buffer's data */
	BlockNumber max_lov_block; /* highest lov block we're seen */
	List *lov_blocks;	/* list of lov blocks we're buffering */
} BMTidBuildBuf;


/*
 * BMTIDBuffer represents TIDs we've buffered for a given bitmap vector --
 * i.e., TIDs for a distinct value in the underlying table. We take advantage
 * of the fact that since we are reading the table from beginning to end
 * TIDs will be ordered.
 */

typedef struct BMTIDBuffer
{
	/* The last two bitmap words */
	BM_HRL_WORD last_compword;
	BM_HRL_WORD last_word;
	bool		is_last_compword_fill;

	uint64          start_tid;  /* starting TID for this buffer */
	uint64			last_tid;	/* most recent tid added */
	int16			curword; /* index into content */
	int16			num_cwords;	/* number of allocated words in content */

	/* the last tids, one for each actual data words */
	uint64         *last_tids;

	/* the header and content words */
	BM_HRL_WORD 	hwords[BM_NUM_OF_HEADER_WORDS];
	BM_HRL_WORD    *cwords;
} BMTIDBuffer;

typedef struct BMBuildLovData
{
	BlockNumber 	lov_block;
	OffsetNumber	lov_off;
} BMBuildLovData;


/*
 * the state for index build 
 */
typedef struct BMBuildState
{
	TupleDesc		bm_tupDesc;
	Relation		bm_lov_heap;
	Relation		bm_lov_index;
	/*
	 * We use this hash to cache lookups of lov blocks for different keys
	 * When one of attribute types can not be hashed, we set this hash
	 * to NULL.
	 */
	HTAB		   *lovitem_hash;

	/**
	 * when lovitem_hash is non-NULL then this will correspond to the
	 *   size of the keys in the hash
	 */
	int lovitem_hashKeySize;

	/*
	 * When the attributes to be indexed can not be hashed, we can not use
	 * the hash for the lov blocks. We have to search through the
	 * btree.
	 */
	ScanKey			bm_lov_scanKeys;
	IndexScanDesc	bm_lov_scanDesc;

	/*
	 * the buffer to store last several tid locations for each distinct
	 * value.
	 */
	BMTidBuildBuf	*bm_tidLocsBuffer;

	double 			ituples;	/* the number of index tuples */
	bool			use_wal;	/* whether or not we write WAL records */
} BMBuildState;

/**
 * The key used inside BMBuildState's lovitem_hash hashtable
 *
 * The caller should assign attributeValueArr and isNullArr to point at the values and isnull array to be hashed
 *
 * When inside the hashtable, attributeValueArr and isNullArr will point to aligned memory within the key block itself
 */
typedef struct BMBuildHashKey
{
    Datum *attributeValueArr;
    bool *isNullArr;
} BMBuildHashKey;

/*
 * Define an iteration result while scanning an BMBatchWords.
 *
 * This result includes the last scan position in an BMBatchWords,
 * and all tids that are generated from previous scan.
 */
typedef struct BMIterateResult
{
	uint64	nextTid; /* the first tid for the next iteration word, should start from 1 */
	uint32	lastScanPos; /* position in the bitmap word we're looking at */
	uint32	lastScanWordNo;	/* offset in BWBatchWords */
	uint64	nextTids[BM_BATCH_TIDS]; /* array of matching TIDs */
	uint32	numOfTids; /* number of TIDs matched */
	uint32	nextTidLoc; /* the next position in 'nextTids' to be read. */
} BMIterateResult;

/*
 * Stores a batch of consecutive bitmap words from a bitmap vector.
 *
 * These bitmap words come from a bitmap vector stored in this bitmap
 * index, or a bitmap vector that is generated by ANDing/ORing several
 * bitmap vectors.
 *
 * This struct also contains information to compute the tid locations
 * for the set bits in these bitmap words.
 */
typedef struct BMBatchWords
{
	uint32	maxNumOfWords;		/* maximum number of words in this list */

	/*
	 * nwordsread and nextread only used for multi bitmap vectors' bitmap union.
	 * If there are more than one bitmap vector matched, we should generate
	 * output bitmap that union all vectors' bitmap.
	 * Since BMBatchWords are under HRL compress format, so each vector's current
	 * read batch words contain different uncompressed words. We should record
	 * the uncompressed word info to help us union bitmap across different vectors.
	 */
	uint64	nwordsread;			/* Number of uncompressed words that have been read already from the begining */
	uint64	nextread;			/* next word to read */

	uint64	firstTid;			/* the TID we're up to, should always point to a word's first tid */
	uint32	startNo;			/* position we're at in cwords */
	uint32	nwords;				/* the number of bitmap words */
	BM_HRL_WORD *hwords; 		/* the header words */
	BM_HRL_WORD *cwords;		/* the actual bitmap words */
} BMBatchWords;

/*
 * Scan opaque data for one bitmap vector.
 *
 * This structure stores a batch of consecutive bitmap words for a
 * bitmap vector that have been read from the disk, and remembers
 * the next reading position for the next batch of consecutive
 * bitmap words.
 */
typedef struct BMVectorData
{
	Buffer			bm_lovBuffer;/* the buffer that contains the LOV item. */
	OffsetNumber	bm_lovOffset;	/* the offset of the LOV item */
	BlockNumber		bm_nextBlockNo; /* the next bitmap page block */

	/* indicate if the last two words in the bitmap has been read. 
	 * These two words are stored inside a BMLovItem. If this value
	 * is true, it means this bitmap vector has no more words.
	 */
	bool			bm_readLastWords;
	BMBatchWords   *bm_batchWords; /* actual bitmap words */

} BMVectorData;
typedef BMVectorData *BMVector;

/*
 * Defines the current position of a scan.
 *
 * For each scan, all related bitmap vectors are read from the bitmap
 * index, and ORed together into a final bitmap vector. The words
 * in each bitmap vector are read in batches. This structure stores
 * the following:
 * (1) words for a final bitmap vector after ORing words from
 *     related bitmap vectors. 
 * (2) tid locations that satisfy the query.
 * (3) One BMVectorData for each related bitmap vector.
 */
typedef struct BMScanPositionData
{
	bool			done;	/* indicate if this scan is over */
	int				nvec;	/* the number of related bitmap vectors */
	/* the words in the final bitmap vector that satisfies the query. */
	BMBatchWords   *bm_batchWords;

	/*
	 * The BMIterateResult instance that contains the final 
	 * tid locations for tuples that satisfy the query.
	 */
	BMIterateResult bm_result;
	BMVector	posvecs;	/* one or more bitmap vectors */
} BMScanPositionData;

typedef BMScanPositionData *BMScanPosition;

typedef struct BMScanOpaqueData
{
	BMScanPosition		bm_currPos;
	bool				cur_pos_valid;
	/* XXX: should we pull out mark pos? */
	BMScanPosition		bm_markPos;
	bool				mark_pos_valid;
} BMScanOpaqueData;

typedef BMScanOpaqueData *BMScanOpaque;

/* public routines */
extern Datum bmhandler(PG_FUNCTION_ARGS);
extern IndexBuildResult *bmbuild(Relation heap, Relation index,
		struct IndexInfo *indexInfo);
extern void bmbuildempty(Relation index);
extern bool bminsert(Relation rel, Datum *values, bool *isnull,
					 ItemPointer ht_ctid, Relation heapRel,
					 IndexUniqueCheck checkUnique,
					 struct IndexInfo *indexInfo);
extern IndexScanDesc bmbeginscan(Relation rel, int nkeys, int norderbys);
extern bool bmgettuple(IndexScanDesc scan, ScanDirection dir);
extern Node *bmgetbitmap(IndexScanDesc scan, Node *tbm);
extern void bmrescan(IndexScanDesc scan, ScanKey scankey, int nscankeys,
		 ScanKey orderbys, int norderbys);
extern void bmendscan(IndexScanDesc scan);
extern void bmmarkpos(IndexScanDesc scan);
extern void bmrestrpos(IndexScanDesc scan);
extern IndexBulkDeleteResult *bmbulkdelete(IndexVacuumInfo *info,
			 IndexBulkDeleteResult *stats,
			 IndexBulkDeleteCallback callback,
			 void *callback_state);
extern IndexBulkDeleteResult *bmvacuumcleanup(IndexVacuumInfo *info,
				IndexBulkDeleteResult *stats);
extern bool bmcanreturn(Relation index, int attno);
extern bytea *bmoptions(Datum reloptions, bool validate);
extern bool bmvalidate(Oid opclassoid);

extern void GetBitmapIndexAuxOids(Relation index, Oid *heapId, Oid *indexId);

/* bitmappages.c */
extern Buffer _bitmap_getbuf(Relation rel, BlockNumber blkno, int access);
extern void _bitmap_wrtbuf(Buffer buf);
extern void _bitmap_relbuf(Buffer buf);
extern void _bitmap_init_lovpage(Relation rel, Buffer buf);
extern void _bitmap_init_bitmappage(Page page);
extern void _bitmap_init_buildstate(Relation index, BMBuildState* bmstate);
extern void _bitmap_cleanup_buildstate(Relation index, BMBuildState* bmstate);
extern void _bitmap_init(Relation indexrel, bool use_wal, bool for_empty);

/* bitmapinsert.c */
extern void _bitmap_buildinsert(Relation rel, ItemPointerData ht_ctid, 
								Datum *attdata, bool *nulls,
							 	BMBuildState *state);
extern void _bitmap_doinsert(Relation rel, ItemPointerData ht_ctid, 
							 Datum *attdata, bool *nulls);
extern void _bitmap_write_alltids(Relation rel, BMTidBuildBuf *tids,
						  		  bool use_wal);

/* bitmaputil.c */
extern BMLOVItem _bitmap_formitem(uint64 currTidNumber);
extern BMMetaPage _bitmap_get_metapage_data(Relation rel, Buffer metabuf);
extern void _bitmap_init_batchwords(BMBatchWords* words,
									uint32	maxNumOfWords,
									MemoryContext mcxt);
extern void _bitmap_copy_batchwords(BMBatchWords *words, BMBatchWords *copyWords);
extern void _bitmap_reset_batchwords(BMBatchWords* words);
extern void _bitmap_cleanup_batchwords(BMBatchWords* words);
extern void _bitmap_cleanup_scanpos(BMVector bmScanPos,
									uint32 numBitmapVectors);
extern uint64 _bitmap_findnexttid(BMBatchWords *words,
								  BMIterateResult *result);
extern void _bitmap_findnexttids(BMBatchWords *words,
								 BMIterateResult *result, uint32 maxTids);
extern void _bitmap_catchup_to_next_tid(BMBatchWords *words, BMIterateResult *result);
#ifdef NOT_USED /* we might use this later */
extern void _bitmap_intersect(BMBatchWords **batches, uint32 numBatches,
						   BMBatchWords *result);
#endif
extern void _bitmap_union(BMBatchWords **batches, uint32 numBatches,
					   BMBatchWords *result);
extern void _bitmap_begin_iterate(BMBatchWords *words, BMIterateResult *result);
extern void _bitmap_log_metapage(Relation rel, ForkNumber fork, Page page);
extern void _bitmap_log_bitmap_lastwords(Relation rel, Buffer lovBuffer,
									 OffsetNumber lovOffset, BMLOVItem lovItem);
extern void _bitmap_log_lovitem	(Relation rel, ForkNumber fork, Buffer lovBuffer,
								 OffsetNumber offset, BMLOVItem lovItem,
								 Buffer metabuf,  bool is_new_lov_blkno);
extern void _bitmap_log_bitmapwords(Relation rel, BMTIDBuffer *buf,
						bool init_first_page, List *xl_bm_bitmapwords, List *bitmapBuffers,
						Buffer lovBuffer, OffsetNumber lovOffset, uint64 tidnum);
extern void _bitmap_log_updatewords(Relation rel,
						Buffer lovBuffer, OffsetNumber lovOffset,
						Buffer firstBuffer, Buffer secondBuffer,
						bool new_lastpage);
extern void _bitmap_log_updateword(Relation rel, Buffer bitmapBuffer, int word_no);

#ifdef DUMP_BITMAPAM_INSERT_RECORDS
extern void _dump_page(char *file, XLogRecPtr recptr, RelFileNode *relfilenode, Buffer buf);
#endif

/* bitmapsearch.c */
extern bool _bitmap_first(IndexScanDesc scan, ScanDirection dir);
extern bool _bitmap_next(IndexScanDesc scan, ScanDirection dir);
extern bool _bitmap_firstbatchwords(IndexScanDesc scan, ScanDirection dir);
extern bool _bitmap_nextbatchwords(IndexScanDesc scan, ScanDirection dir);
extern void _bitmap_findbitmaps(IndexScanDesc scan, ScanDirection dir);



/* bitmapattutil.c */
extern void _bitmap_create_lov_heapandindex(Relation rel,
											Oid *lovHeapOid,
											Oid *lovIndexOid);
extern void _bitmap_open_lov_heapandindex(Relation rel, BMMetaPage metapage,
						 Relation *lovHeapP, Relation *lovIndexP,
						 LOCKMODE lockMode);
extern void _bitmap_insert_lov(Relation lovHeap, Relation lovIndex,
							   Datum *datum, bool *nulls, bool use_wal);
extern void _bitmap_close_lov_heapandindex(Relation lovHeap, 
										Relation lovIndex, LOCKMODE lockMode);
extern bool _bitmap_findvalue(Relation lovHeap, Relation lovIndex,
							 ScanKey scanKey, IndexScanDesc scanDesc,
							 BlockNumber *lovBlock, bool *blockNull,
							 OffsetNumber *lovOffset, bool *offsetNull);

#endif							/* BITMAP_PRIVATE_H */
