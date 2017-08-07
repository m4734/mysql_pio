#include "dict0dict.h"
#include "dict0boot.h"
#include "trx0undo.h"
#include "trx0trx.h"
#include "btr0btr.h"
#include "btr0cur.h"
#include "btr0sea.h"
#include "gis0rtree.h"
#include "mach0data.h"
#include "que0que.h"
#include "row0upd.h"
#include "row0row.h"
#include "row0vers.h"
#include "rem0cmp.h"
#include "lock0lock.h"
#include "eval0eval.h"
#include "pars0sym.h"
#include "pars0pars.h"
#include "row0mysql.h"
#include "read0read.h"
#include "buf0lru.h"
#include "ha_prototypes.h"
#include "srv0mon.h"
#include "ut0new.h"



#include "cgmin0cgmin.h"
#include "cgmin0cgmin.ic"

//cgmin
#if 1
void set_pcur_pos_pio_func(int pio_t,btr_pcur_t* pcur_pio,int* page_id_pio,btr_pcur_t* temp_pcur_pio,
	bool		from_left,	/*!< in: true if open to the low end,
					false if to the high end */
	dict_index_t*	index,		/*!< in: index */
	ulint		latch_mode,	/*!< in: latch mode */
	btr_cur_t*	cursor,		/*!< in/out: cursor */
	ulint		level,		/*!< in: level to search for
					(0=leaf). */
	const char*	file,		/*!< in: file name */
	ulint		line,		/*!< in: line where called */
	mtr_t*		mtr)		/*!< in/out: mini-transaction */
{
	page_cur_t*	page_cursor;
	ulint		node_ptr_max_size = UNIV_PAGE_SIZE / 2;
	ulint		height;
	ulint		root_height = 0; /* remove warning */
	rec_t*		node_ptr;
	ulint		estimate;
	ulint		savepoint;
	ulint		upper_rw_latch, root_leaf_rw_latch;
	btr_intention_t	lock_intention;
	buf_block_t*	tree_blocks[BTR_MAX_LEVELS];
	ulint		tree_savepoints[BTR_MAX_LEVELS];
	ulint		n_blocks = 0;
	ulint		n_releases = 0;
	mem_heap_t*	heap		= NULL;
	ulint		offsets_[REC_OFFS_NORMAL_SIZE];
	ulint*		offsets		= offsets_;
	rec_offs_init(offsets_);

	estimate = latch_mode & BTR_ESTIMATE;
	latch_mode &= ~BTR_ESTIMATE;

	ut_ad(level != ULINT_UNDEFINED);

	bool	s_latch_by_caller;

	s_latch_by_caller = latch_mode & BTR_ALREADY_S_LATCHED;
	latch_mode &= ~BTR_ALREADY_S_LATCHED;

	lock_intention = btr_cur_get_and_clear_intention(&latch_mode);

	ut_ad(!(latch_mode & BTR_MODIFY_EXTERNAL));

	/* This function doesn't need to lock left page of the leaf page */
	if (latch_mode == BTR_SEARCH_PREV) {
		latch_mode = BTR_SEARCH_LEAF;
	} else if (latch_mode == BTR_MODIFY_PREV) {
		latch_mode = BTR_MODIFY_LEAF;
	}

	/* Store the position of the tree latch we push to mtr so that we
	know how to release it when we have latched the leaf node */

	savepoint = mtr_set_savepoint(mtr);

	switch (latch_mode) {
	case BTR_CONT_MODIFY_TREE:
	case BTR_CONT_SEARCH_TREE:
		upper_rw_latch = RW_NO_LATCH;
		break;
	case BTR_MODIFY_TREE:
		/* Most of delete-intended operations are purging.
		Free blocks and read IO bandwidth should be prior
		for them, when the history list is glowing huge. */
		if (lock_intention == BTR_INTENTION_DELETE
		    && trx_sys->rseg_history_len > BTR_CUR_FINE_HISTORY_LENGTH
		    && buf_get_n_pending_read_ios()) {
			mtr_x_lock(dict_index_get_lock(index), mtr);
		} else {
			mtr_sx_lock(dict_index_get_lock(index), mtr);
		}
		upper_rw_latch = RW_X_LATCH;
		break;
	default:
		ut_ad(!s_latch_by_caller
		      || mtr_memo_contains_flagged(mtr,
						 dict_index_get_lock(index),
						 MTR_MEMO_SX_LOCK
						 | MTR_MEMO_S_LOCK));
		if (!srv_read_only_mode) {
			if (!s_latch_by_caller) {
				/* BTR_SEARCH_TREE is intended to be used with
				BTR_ALREADY_S_LATCHED */
				ut_ad(latch_mode != BTR_SEARCH_TREE);

				mtr_s_lock(dict_index_get_lock(index), mtr);
			}
			upper_rw_latch = RW_S_LATCH;
		} else {
			upper_rw_latch = RW_NO_LATCH;
		}
	}
	root_leaf_rw_latch = btr_cur_latch_for_root_leaf(latch_mode);

	page_cursor = btr_cur_get_page_cur(cursor);
	cursor->index = index;


	page_id_t		page_id(dict_index_get_space(index),
					dict_index_get_page(index));
	const page_size_t&	page_size = dict_table_page_size(index->table);


//cgmin
	page_id_t temp_page_id(page_id.space(),page_id.page_no());
	int level_node_num;
//   temp_page_id.copy_from(page_id);

	if (root_leaf_rw_latch == RW_X_LATCH) {
		node_ptr_max_size = dict_index_node_ptr_max_size(index);
	}

	height = ULINT_UNDEFINED;

	for (;;) {
		buf_block_t*	block;
		page_t*		page;
		ulint		rw_latch;

		ut_ad(n_blocks < BTR_MAX_LEVELS);

		if (height != 0
		    && (latch_mode != BTR_MODIFY_TREE
			|| height == level)) {
			rw_latch = upper_rw_latch;
		} else {
			rw_latch = RW_NO_LATCH;
		}

		tree_savepoints[n_blocks] = mtr_set_savepoint(mtr);
		block = buf_page_get_gen(page_id, page_size, rw_latch, NULL,
					 BUF_GET, file, line, mtr);
		tree_blocks[n_blocks] = block;

		page = buf_block_get_frame(block);

//cgmin
		buf_block_t* temp_block;
//		page_t* temp_page;
		btr_cur_t* temp_cursor=btr_pcur_get_btr_cur(temp_pcur_pio);
		temp_cursor->index = index;
		page_cur_t* temp_page_cursor=btr_cur_get_page_cur(temp_cursor);

		level_node_num=1;

		temp_block = buf_page_get_gen(page_id, page_size, rw_latch, NULL,
					 BUF_GET, file, line, mtr);
//		temp_page = buf_block_get_frame(block);
		page_cur_set_before_first(temp_block, temp_page_cursor);
		while(btr_pcur_is_after_last_in_tree(temp_pcur_pio,mtr) == FALSE)
		{
			btr_pcur_move_to_next_page(temp_pcur_pio,mtr);
			level_node_num++;
		}
		if (level_node_num <= pio_t)
			level = height;

		if (height == ULINT_UNDEFINED
		    && btr_page_get_level(page, mtr) == 0
		    && rw_latch != RW_NO_LATCH
		    && rw_latch != root_leaf_rw_latch) {
			/* We should retry to get the page, because the root page
			is latched with different level as a leaf page. */
			ut_ad(root_leaf_rw_latch != RW_NO_LATCH);
			ut_ad(rw_latch == RW_S_LATCH);

			ut_ad(n_blocks == 0);
			mtr_release_block_at_savepoint(
				mtr, tree_savepoints[n_blocks],
				tree_blocks[n_blocks]);

			upper_rw_latch = root_leaf_rw_latch;
			continue;
		}

		ut_ad(fil_page_index_page_check(page));
		ut_ad(index->id == btr_page_get_index_id(page));

		if (height == ULINT_UNDEFINED) {
			/* We are in the root node */

			height = btr_page_get_level(page, mtr);
			root_height = height;
			ut_a(height >= level);
		} else {
			/* TODO: flag the index corrupted if this fails */
			ut_ad(height == btr_page_get_level(page, mtr));
		}

		if (height == level) {
			if (srv_read_only_mode) {
				btr_cur_latch_leaves(
					block, page_id, page_size,
					latch_mode, cursor, mtr);
			} else if (height == 0) {
				if (rw_latch == RW_NO_LATCH) {
					btr_cur_latch_leaves(
						block, page_id, page_size,
						latch_mode, cursor, mtr);
				}
				/* In versions <= 3.23.52 we had
				forgotten to release the tree latch
				here. If in an index scan we had to
				scan far to find a record visible to
				the current transaction, that could
				starve others waiting for the tree
				latch. */

				switch (latch_mode) {
				case BTR_MODIFY_TREE:
				case BTR_CONT_MODIFY_TREE:
				case BTR_CONT_SEARCH_TREE:
					break;
				default:
					if (!s_latch_by_caller) {
						/* Release the tree s-latch */
						mtr_release_s_latch_at_savepoint(
							mtr, savepoint,
							dict_index_get_lock(
								index));
					}

					/* release upper blocks */
					for (; n_releases < n_blocks;
					     n_releases++) {
						mtr_release_block_at_savepoint(
							mtr,
							tree_savepoints[
								n_releases],
							tree_blocks[
								n_releases]);
					}
				}
			} else { /* height != 0 */
				/* We already have the block latched. */
				ut_ad(latch_mode == BTR_SEARCH_TREE);
				ut_ad(s_latch_by_caller);
				ut_ad(upper_rw_latch == RW_S_LATCH);

				ut_ad(mtr_memo_contains(mtr, block,
							upper_rw_latch));

				if (s_latch_by_caller) {
					/* to exclude modifying tree operations
					should sx-latch the index. */
					ut_ad(mtr_memo_contains(
						mtr,
						dict_index_get_lock(index),
						MTR_MEMO_SX_LOCK));
					/* because has sx-latch of index,
					can release upper blocks. */
					for (; n_releases < n_blocks;
					     n_releases++) {
						mtr_release_block_at_savepoint(
							mtr,
							tree_savepoints[
								n_releases],
							tree_blocks[
								n_releases]);
					}
				}
			}
		}

		if (from_left) {
			page_cur_set_before_first(block, page_cursor);
		} else {
			page_cur_set_after_last(block, page_cursor);
		}

		if (height == level) {
			if (estimate) {
				btr_cur_add_path_info(cursor, height,
						      root_height);
			}

			break;
		}

		ut_ad(height > 0);

		if (from_left) {
			page_cur_move_to_next(page_cursor);
		} else {
			page_cur_move_to_prev(page_cursor);
		}

		if (estimate) {
			btr_cur_add_path_info(cursor, height, root_height);
		}

		height--;

		node_ptr = page_cur_get_rec(page_cursor);
		offsets = rec_get_offsets(node_ptr, cursor->index, offsets,
					  ULINT_UNDEFINED, &heap);

		/* If the rec is the first or last in the page for
		pessimistic delete intention, it might cause node_ptr insert
		for the upper level. We should change the intention and retry.
		*/
		if (latch_mode == BTR_MODIFY_TREE
		    && btr_cur_need_opposite_intention(
			page, lock_intention, node_ptr)) {

			ut_ad(upper_rw_latch == RW_X_LATCH);
			/* release all blocks */
			for (; n_releases <= n_blocks; n_releases++) {
				mtr_release_block_at_savepoint(
					mtr, tree_savepoints[n_releases],
					tree_blocks[n_releases]);
			}

			lock_intention = BTR_INTENTION_BOTH;

			page_id.set_page_no(dict_index_get_page(index));

			height = ULINT_UNDEFINED;

			n_blocks = 0;
			n_releases = 0;

			continue;
		}

		if (latch_mode == BTR_MODIFY_TREE
		    && !btr_cur_will_modify_tree(
				cursor->index, page, lock_intention, node_ptr,
				node_ptr_max_size, page_size, mtr)) {
			ut_ad(upper_rw_latch == RW_X_LATCH);
			ut_ad(n_releases <= n_blocks);

			/* we can release upper blocks */
			for (; n_releases < n_blocks; n_releases++) {
				if (n_releases == 0) {
					/* we should not release root page
					to pin to same block. */
					continue;
				}

				/* release unused blocks to unpin */
				mtr_release_block_at_savepoint(
					mtr, tree_savepoints[n_releases],
					tree_blocks[n_releases]);
			}
		}

		if (height == level
		    && latch_mode == BTR_MODIFY_TREE) {
			ut_ad(upper_rw_latch == RW_X_LATCH);
			/* we should sx-latch root page, if released already.
			It contains seg_header. */
			if (n_releases > 0) {
				mtr_block_sx_latch_at_savepoint(
					mtr, tree_savepoints[0],
					tree_blocks[0]);
			}

			/* x-latch the branch blocks not released yet. */
			for (ulint i = n_releases; i <= n_blocks; i++) {
				mtr_block_x_latch_at_savepoint(
					mtr, tree_savepoints[i],
					tree_blocks[i]);
			}
		}

		temp_page_id.copy_from(page_id);
		/* Go to the child node */
		page_id.set_page_no(
			btr_node_ptr_get_child_page_no(node_ptr, offsets));

		n_blocks++;
	}

	if (heap) {
		mem_heap_free(heap);
	}

	//cgmin

/*
		ulint		rw_latch;

		ut_ad(n_blocks < BTR_MAX_LEVELS);

		if (height != 0
		    && (latch_mode != BTR_MODIFY_TREE
			|| height == level)) {
			rw_latch = upper_rw_latch;
		} else {
			rw_latch = RW_NO_LATCH;
		}
*/
	if (level_node_num == pio_t)
		temp_page_id.copy_from(page_id);
	buf_block_t* temp_block;
//	page_t* temp_page;
//	page_cur_t* temp_page_cursor=*page_cursor;
//	btr_cur_t* temp_cursor=*cursor;
	btr_cur_t* temp_cursor=btr_pcur_get_btr_cur(temp_pcur_pio);
	temp_cursor->index = index;
	page_cur_t* temp_page_cursor=btr_cur_get_page_cur(temp_cursor);

	level_node_num=0;

	temp_block = buf_page_get_gen(temp_page_id, page_size, /*rw_latch*/ RW_NO_LATCH, NULL,
				 BUF_GET, file, line, mtr);
//	temp_page = buf_block_get_frame(block);
	page_cur_set_before_first(temp_block, temp_page_cursor);
//	btr_cur_get_page_cur(temp_cursor)->copy_from(temp_page_cursor);
	while(btr_pcur_is_after_last_in_tree(temp_pcur_pio,mtr) == FALSE)
	{
	buf_block_t* temp_block2;
	btr_cur_t* temp_cursor2=btr_pcur_get_btr_cur(&pcur_pio[level_node_num]);
	temp_cursor2->index = index;
	page_cur_t* temp_page_cursor2=btr_cur_get_page_cur(temp_cursor2);
	page_id_t temp_page_id2(temp_page_id.space(),temp_page_id.page_no());
//   temp_page_id2.copy_from(temp_page_id);

		int height2;
		for (height2=height;height2>0;--height2)
		{
			node_ptr = page_cur_get_rec(temp_page_cursor2);
			offsets = rec_get_offsets(node_ptr, temp_cursor2->index, offsets,
					  ULINT_UNDEFINED, &heap);
			temp_page_id2.set_page_no(btr_node_ptr_get_child_page_no(node_ptr,offsets));

			temp_block2 = buf_page_get_gen(temp_page_id2, page_size, /*rw_latch*/ RW_NO_LATCH, NULL,
				 BUF_GET, file, line, mtr);
			page_cur_set_before_first(temp_block2, temp_page_cursor2);
		}

//		btr_cur_get_page_cur(&pcur_pio[level_node_num])->copy_from(temp_page_cursor);

		page_id_pio[level_node_num] = temp_block2->page.id.page_no(); 
		btr_pcur_move_to_next_page(temp_pcur_pio,mtr);
		temp_block = btr_pcur_get_block(temp_pcur_pio);
		temp_page_id.copy_from(temp_block->page.id);

		level_node_num++;
	}


//cgmin
DBUG_VOID_RETURN;

}

#endif

