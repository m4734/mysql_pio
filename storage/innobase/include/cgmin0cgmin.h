#include "btr0pcur.cc"
#include "btr0cur.cc"

#if 0
#include "univ.i"
#include "dict0dict.h"
#include "data0data.h"
#include "mtr0mtr.h"
#include "page0cur.h"
#include "btr0cur.h"
#include "btr0btr.h"
#include "btr0types.h"
#include "gis0rtree.h"


#include "btr0cur.h"
#include "btr0pcur.h"
#include "btr0cur.h"

#ifdef UNIV_NONINL
#include "btr0cur.ic"
#endif

#include "row0upd.h"
#ifndef UNIV_HOTBACKUP
#include "mtr0log.h"
#include "page0page.h"
#include "page0zip.h"
#include "rem0rec.h"
#include "rem0cmp.h"
#include "buf0lru.h"
#include "btr0btr.h"
#include "btr0sea.h"
#include "row0log.h"
#include "row0purge.h"
#include "row0upd.h"
#include "trx0rec.h"
#include "trx0roll.h"
#include "que0que.h"
#include "row0row.h"
#include "srv0srv.h"
#include "ibuf0ibuf.h"
#include "lock0lock.h"
#include "zlib.h"
#include "srv0start.h"

/** Buffered B-tree operation types, introduced as part of delete buffering. */
enum btr_op_t {
	BTR_NO_OP = 0,			/*!< Not buffered */
	BTR_INSERT_OP,			/*!< Insert, do not ignore UNIQUE */
	BTR_INSERT_IGNORE_UNIQUE_OP,	/*!< Insert, ignoring UNIQUE */
	BTR_DELETE_OP,			/*!< Purge a delete-marked record */
	BTR_DELMARK_OP			/*!< Mark a record for deletion */
};

/** Modification types for the B-tree operation. */
enum btr_intention_t {
	BTR_INTENTION_DELETE,
	BTR_INTENTION_BOTH,
	BTR_INTENTION_INSERT
};
#if BTR_INTENTION_DELETE > BTR_INTENTION_BOTH
#error "BTR_INTENTION_DELETE > BTR_INTENTION_BOTH"
#endif
#if BTR_INTENTION_BOTH > BTR_INTENTION_INSERT
#error "BTR_INTENTION_BOTH > BTR_INTENTION_INSERT"
#endif

/** For the index->lock scalability improvement, only possibility of clear
performance regression observed was caused by grown huge history list length.
That is because the exclusive use of index->lock also worked as reserving
free blocks and read IO bandwidth with priority. To avoid huge glowing history
list as same level with previous implementation, prioritizes pessimistic tree
operations by purge as the previous, when it seems to be growing huge.

 Experimentally, the history list length starts to affect to performance
throughput clearly from about 100000. */
#define BTR_CUR_FINE_HISTORY_LENGTH	100000

/** Number of searches down the B-tree in btr_cur_search_to_nth_level(). */
ulint	btr_cur_n_non_sea	= 0;
/** Number of successful adaptive hash index lookups in
btr_cur_search_to_nth_level(). */
ulint	btr_cur_n_sea		= 0;
/** Old value of btr_cur_n_non_sea.  Copied by
srv_refresh_innodb_monitor_stats().  Referenced by
srv_printf_innodb_monitor(). */
ulint	btr_cur_n_non_sea_old	= 0;
/** Old value of btr_cur_n_sea.  Copied by
srv_refresh_innodb_monitor_stats().  Referenced by
srv_printf_innodb_monitor(). */
ulint	btr_cur_n_sea_old	= 0;

#ifdef UNIV_DEBUG
/* Flag to limit optimistic insert records */
uint	btr_cur_limit_optimistic_insert_debug = 0;
#endif /* UNIV_DEBUG */

/** In the optimistic insert, if the insert does not fit, but this much space
can be released by page reorganize, then it is reorganized */
#define BTR_CUR_PAGE_REORGANIZE_LIMIT	(UNIV_PAGE_SIZE / 32)

/** The structure of a BLOB part header */
/* @{ */
/*--------------------------------------*/
#define BTR_BLOB_HDR_PART_LEN		0	/*!< BLOB part len on this
						page */
#define BTR_BLOB_HDR_NEXT_PAGE_NO	4	/*!< next BLOB part page no,
						FIL_NULL if none */
/*--------------------------------------*/
#define BTR_BLOB_HDR_SIZE		8	/*!< Size of a BLOB
						part header, in bytes */

/** Estimated table level stats from sampled value.
@param value sampled stats
@param index index being sampled
@param sample number of sampled rows
@param ext_size external stored data size
@param not_empty table not empty
@return estimated table wide stats from sampled value */
#define BTR_TABLE_STATS_FROM_SAMPLE(value, index, sample, ext_size, not_empty) \
	(((value) * static_cast<int64_t>(index->stat_n_leaf_pages) \
	  + (sample) - 1 + (ext_size) + (not_empty)) / ((sample) + (ext_size)))

/* @} */
#endif /* !UNIV_HOTBACKUP */
#endif
//cgmin
void set_pcur_pos_pio_func(int pio_t,btr_pcur_t* pcur_pio,ulint* page_id_pio,btr_pcur_t* temp_pcur_pio,
	bool		from_left,	/*!< in: true if open to the low end,
					false if to the high end */
	dict_index_t*	index,		/*!< in: index */
	ulint		latch_mode,	/*!< in: latch mode */
	btr_cur_t*	cursor,		/*!< in/out: cursor */
	ulint		level,		/*!< in: level to search for
					(0=leaf). */
	const char*	file,		/*!< in: file name */
	ulint		line,		/*!< in: line where called */
	mtr_t*		mtr);		/*!< in/out: mini-transaction */
#define set_pcur_pos_pio(pt,pp,pip,tpp,f,i,l,c,lv,m)			\
	set_pcur_pos_pio_func(pt,pp,pip,tpp,f,i,l,c,lv,__FILE__,__LINE__,m)

UNIV_INLINE
void prepare_pio(int pio_t,btr_pcur_t *pcur_pio,ulint *page_id_pio,
	bool		from_left,	/*!< in: true if open to the low end,
					false if to the high end */
	dict_index_t*	index,		/*!< in: index */
	ulint		latch_mode,	/*!< in: latch mode */
	btr_pcur_t*	pcur,		/*!< in/out: cursor */
	bool		init_pcur,	/*!< in: whether to initialize pcur */
	ulint		level,		/*!< in: level to search for
					(0=leaf) */
	mtr_t*		mtr)		/*!< in/out: mini-transaction */
;
UNIV_INLINE
void do_pio(int pio_t,btr_pcur_t *pcur_pio,ulint *page_id_pio,mtr_t* mtr);
UNIV_INLINE
void close_pio(int pio_t,btr_pcur_t *pcur_pio,btr_pcur_t* pcur);

