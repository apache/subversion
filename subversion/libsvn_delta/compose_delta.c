/*
 * compose_delta.c:  Delta window composition.
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */


#include <assert.h>

#include <apr_general.h>        /* For APR_INLINE */

#include "svn_delta.h"
#include "svn_pools.h"
#include "delta.h"


/* ==================================================================== */
/* Support for efficient small-block allocation from pools. */

/* The following structs will be allocated and freed often: */

/* A node in the range index tree. */
typedef struct range_index_node_t range_index_node_t;
struct range_index_node_t
{
  /* 'offset' and 'limit' define the range in the source window. */
  apr_off_t offset;
  apr_off_t limit;

  /* 'target_offset' is where that range is represented in the target. */
  apr_off_t target_offset;

  /* 'left' and 'right' link the node into a splay tree. */
  range_index_node_t *left, *right;

  /* 'prev' and 'next' link it into an ordered, doubly-linked list. */
  range_index_node_t *prev, *next;
};

/* A node in a list of ranges for source and target op copies. */
enum range_kind
  {
    range_from_source,
    range_from_target
  };

typedef struct range_list_node_t range_list_node_t;
struct range_list_node_t
{
  /* Where does the range come from?
     'offset' and 'limit' always refer to the "virtual" source data
     for the second delta window. For a target range, the actual
     offset to use for generating the target op is 'target_offset';
     that field isn't used by source ranges. */
  enum range_kind kind;

  /* 'offset' and 'limit' define the range. */
  apr_off_t offset;
  apr_off_t limit;

  /* 'target_offset' is the start of the range in the target. */
  apr_off_t target_offset;

  /* 'prev' and 'next' link the node into an ordered, doubly-linked list. */
  range_list_node_t *prev, *next;
};


/* This is what will be allocated: */
typedef union alloc_block_t alloc_block_t;
union alloc_block_t
{
  range_index_node_t index_node;
  range_list_node_t list_node;

  /* Links free blocks into a freelist. */
  alloc_block_t *next_free;
};


/* Allocate a block. */
static APR_INLINE void *
alloc_block (apr_pool_t *pool, alloc_block_t **free_list)
{
  alloc_block_t *block;
  if (*free_list == NULL)
    block = apr_palloc(pool, sizeof(*block));
  else
    {
      block = *free_list;
      *free_list = block->next_free;
    }
  return block;
}

/* Return the block back to the free list. */
static APR_INLINE void
free_block (void *ptr, alloc_block_t **free_list)
{
  /* Wrapper functions take care of type safety. */
  alloc_block_t *const block = ptr;
  block->next_free = *free_list;
  *free_list = block;
}



/* ==================================================================== */
/* Mapping offsets in the target streem to txdelta ops. */

typedef struct offset_index_node_t
{
  /* Offset into the target stream defined by this op. */
  apr_off_t offset;

  /* Position in the new-data stream, for svn_txdelta_new ops. */
  apr_off_t new_data_pos;
} offset_index_node_t;

typedef struct offset_index_t
{
  int length;
  offset_index_node_t *offs;
} offset_index_t;

/* Create an index mapping target stream offsets to delta ops in
   WINDOW. Allocate from POOL. */

static offset_index_t *
create_offset_index (const svn_txdelta_window_t *window,
                     apr_pool_t *pool)
{
  offset_index_t *ndx = apr_palloc(pool, sizeof(*ndx));
  apr_off_t offset = 0;
  apr_off_t new_data_pos = 0;
  int i;

  ndx->length = window->num_ops;
  ndx->offs = apr_palloc(pool, (ndx->length + 1) * sizeof(*ndx->offs));

  for (i = 0; i < ndx->length; ++i)
    {
      const svn_txdelta_op_t *const op = &window->ops[i];
      offset_index_node_t *const node = &ndx->offs[i];

      node->offset = offset;
      offset += op->length;
      if (op->action_code == svn_txdelta_new)
        {
          node->new_data_pos = new_data_pos;
          new_data_pos += op->length;
        }
      else
        node->new_data_pos = -1;
    }
  ndx->offs[ndx->length].offset = offset;
  ndx->offs[ndx->length].new_data_pos = -1;

  return ndx;
}

/* Find the index of the delta op thet defines that data at OFFSET in
   NDX. */

static int
search_offset_index (offset_index_t *ndx, apr_off_t offset)
{
  int lo, hi, op;

  assert(offset >= 0);
  assert(offset < ndx->offs[ndx->length].offset);

  for (lo = 0, hi = ndx->length, op = (lo + hi)/2;
       lo < hi;
       op = (lo + hi)/2)
    {
      const apr_off_t this_offset = ndx->offs[op].offset;
      const apr_off_t next_offset = ndx->offs[op + 1].offset;
      if (offset < this_offset)
        hi = op;
      else if (offset > next_offset)
        lo = op;
      else
        {
          /* this_offset <= offset <= next_offset */
          if (offset == next_offset)
            ++op;
          break;
        }
    }

  assert(ndx->offs[op].offset <= offset && offset < ndx->offs[op + 1].offset);
  return op;
}



/* ==================================================================== */
/* Mapping ranges in the source stream to ranges in the composed delta. */

/* The range index tree. */
typedef struct range_index_t
{
  range_index_node_t *tree;
  alloc_block_t *free_list;
  apr_pool_t *pool;
} range_index_t;

/* Create a range index tree. Allocate from POOL. */
static range_index_t *
create_range_index (apr_pool_t *pool)
{
  range_index_t *ndx = apr_palloc(pool, sizeof(*ndx));
  ndx->tree = NULL;
  ndx->pool = pool;
  ndx->free_list = NULL;
  return ndx;
}

/* Allocate a node for the range index tree. */
static range_index_node_t *
alloc_range_index_node (range_index_t *ndx,
                        apr_off_t offset,
                        apr_off_t limit,
                        apr_off_t target_offset)
{
  range_index_node_t *const node = alloc_block(ndx->pool, &ndx->free_list);
  node->offset = offset;
  node->limit = limit;
  node->target_offset = target_offset;
  node->left = node->right = NULL;
  node->prev = node->next = NULL;
  return node;
}

/* Free a node from the range index tree. */
static void
free_range_index_node (range_index_t *ndx, range_index_node_t *node)
{
  if (node->next)
    node->next->prev = node->prev;
  if (node->prev)
    node->prev->next = node->next;
  free_block(node, &ndx->free_list);
}


/* Splay the index tree, using OFFSET as the key. */

static void
splay_range_index (apr_off_t offset, range_index_t *ndx)
{
  range_index_node_t *tree = ndx->tree;
  range_index_node_t scratch_node;
  range_index_node_t *left, *right;

  if (tree == NULL)
    return;

  scratch_node.left = scratch_node.right = NULL;
  left = right = &scratch_node;

  for (;;)
    {
      if (offset < tree->offset)
        {
          if (tree->left != NULL
              && offset < tree->left->offset)
            {
              /* Right rotation */
              range_index_node_t *const node = tree->left;
              tree->left = node->right;
              node->right = tree;
              tree = node;
	    }
          if (tree->left == NULL)
            break;

          /* Remember the right subtree */
          right->left = tree;
          right = tree;
          tree = tree->left;
	}
      else if (offset > tree->offset)
        {
          if (tree->right != NULL
              && offset > tree->right->offset)
            {
              /* Left rotation */
              range_index_node_t *const node = tree->right;
              tree->right = node->left;
              node->left = tree;
              tree = node;
	    }
          if (tree->right == NULL)
            break;

          /* Remember the left subtree */
          left->right = tree;
          left = tree;
          tree = tree->right;
	}
      else
        break;
    }

  /* Link in the left and right subtrees */
  left->right = tree->left;
  right->left = tree->right;
  tree->left  = scratch_node.right;
  tree->right = scratch_node.left;

  /* The basic top-down splay is finished, but we may still need to
     turn the tree around. What we want is to put the node with the
     largest offset where node->offset <= offset at the top of the
     tree, so that we can insert the new data (or search for existing
     ranges) to the right of the root. This makes cleaning up the
     tree after an insert much simpler, and -- incidentally -- makes
     the whole range index magic work. */
  if (offset < tree->offset && tree->left != NULL)
    {
      if (tree->left->right == NULL)
        {
          /* A single right rotation is enough. */
          range_index_node_t *const node = tree->left;
          tree->left = node->right; /* Which is always NULL. */
          node->right = tree;
          tree = node;
        }
      else
        {
          /* Slide down to the rightmost node in the left subtree. */
          range_index_node_t **nodep = &tree->left;
          while ((*nodep)->right != NULL)
            nodep = &(*nodep)->right;

          /* Now move this node to root in one giant promotion. */
          right = tree;
          left = tree->left;
          tree = *nodep;
          *nodep = tree->left;
          right->left = tree->right; /* Which is always NULL, too. */
          tree->left = left;
          tree->right = right;
        }
    }

  /* Sanity check ... */
  assert ((offset >= tree->offset)
          || ((tree->left == NULL)
              && (tree->prev == NULL)));
  ndx->tree = tree;
}


/* Remove all ranges from NDX that fall into the root's range.  To
   keep the range index as small as possible, we must also remove
   nodes that don't fall into the new range, but have become redundant
   because the new range overlaps the beginning of the next range.
   Like this:

       new-range: |-----------------|
         range-1:         |-----------------|
         range-2:                |--------------------|

   Before new-range was inserted, range-1 and range-2 were both
   necessary. Now the union of new-range and range-2 completely covers
   range-1, which has become redundant now.

   FIXME: But, of course, there's a catch. range-1 must still remain
   in the tree if we want to optimize the number of target copy ops in
   the case were a copy falls within range-1, but starts before
   range-2 and ends after new-range. */

static void
delete_subtree (range_index_t *ndx, range_index_node_t *node)
{
  if (node != NULL)
    {
      delete_subtree(ndx, node->left);
      delete_subtree(ndx, node->right);
      free_range_index_node(ndx, node);
    }
}

static void
clean_tree (range_index_t *ndx, apr_off_t limit)
{
  apr_off_t top_offset = limit + 1;
  range_index_node_t **nodep = &ndx->tree->right;
  while (*nodep != NULL)
    {
      range_index_node_t *const node = *nodep;
      apr_off_t const offset =
        (node->right != NULL && node->right->offset < top_offset
         ? node->right->offset
         : top_offset);

      if (node->limit <= limit
          || (node->offset < limit && offset <= limit))
        {
          *nodep = node->right;
          node->right = NULL;
          delete_subtree(ndx, node);
        }
      else
        {
          top_offset = node->offset;
          nodep = &node->left;
        }
    }
}


/* Add a range [OFFSET, LIMIT) into NDX. If NDX already contains a
   range that encloses [OFFSET, LIMIT), do nothing. Otherwise, remove
   all ranges from NDX that are superseded by the new range.
   NOTE: The range index must be splayed to OFFSET! */

static void
insert_range (apr_off_t offset, apr_off_t limit, apr_off_t target_offset,
              range_index_t *ndx)
{
  range_index_node_t *node = NULL;

  if (ndx->tree == NULL)
    {
      node = alloc_range_index_node(ndx, offset, limit, target_offset);
      ndx->tree = node;
    }
  else
    {
      if (offset == ndx->tree->offset
          && limit > ndx->tree->limit)
        {
          ndx->tree->limit = limit;
          clean_tree(ndx, limit);
        }
      else if (offset > ndx->tree->offset
               && limit > ndx->tree->limit)
        {
          /* We have to make the same sort of checks as clean_tree()
             does for superseded ranges. Have to merge these someday. */

          const svn_boolean_t insert_range_p =
            (!ndx->tree->next
             || ndx->tree->limit < ndx->tree->next->offset
             || limit > ndx->tree->next->limit);

          if (insert_range_p)
            {
              /* Again, we have to check if the new node and the one
                 to the left of the root override root's range. */
              if (ndx->tree->prev && ndx->tree->prev->limit >= offset)
                {
                  /* Replace the data in the splayed node. */
                  ndx->tree->offset = offset;
                  ndx->tree->limit = limit;
                  ndx->tree->target_offset = target_offset;
                }
              else
                {
                  /* Insert the range to the right of the splayed node. */
                  node = alloc_range_index_node(ndx, offset, limit,
                                                target_offset);
                  if ((node->next = ndx->tree->next) != NULL)
                    node->next->prev = node;
                  ndx->tree->next = node;
                  node->prev = ndx->tree;

                  node->right = ndx->tree->right;
                  ndx->tree->right = NULL;
                  node->left = ndx->tree;
                  ndx->tree = node;
                }
              clean_tree(ndx, limit);
            }
          else
            /* Ignore the range */;
        }
      else if (offset < ndx->tree->offset)
        {
          assert(ndx->tree->left == NULL);

          /* Insert the range left of the splayed node */
          node = alloc_range_index_node(ndx, offset, limit, target_offset);
          node->left = node->prev = NULL;
          node->right = node->next = ndx->tree;
          ndx->tree = node->next->prev = node;
          clean_tree(ndx, limit);
        }
      else
        /* Ignore the range */;
    }
}



/* ==================================================================== */
/* Juggling with lists of ranges. */

/* Allocate a node and add it to the range list. LIST is the head of
   the range list, TAIL is the last node in the list. NDX holds the
   freelist; OFFSET, LIMIT and KIND are node data. */
static range_list_node_t *
alloc_range_list (range_list_node_t **list,
                  range_list_node_t **tail,
                  range_index_t *ndx,
                  enum range_kind kind,
                  apr_off_t offset,
                  apr_off_t limit,
                  apr_off_t target_offset)
{
  range_list_node_t *const node = alloc_block(ndx->pool, &ndx->free_list);
  node->kind = kind;
  node->offset = offset;
  node->limit = limit;
  node->target_offset = target_offset;
  if (*list == NULL)
    {
      node->prev = node->next = NULL;
      *list = *tail = node;
    }
  else
    {
      node->prev = *tail;
      node->next = NULL;
      (*tail)->next = node;
      *tail = node;
    }
  return *list;
}

/* Free a range list. LIST is the head of the list, NDX holds the freelist. */
static void
free_range_list (range_list_node_t *list, range_index_t *ndx)
{
  while (list)
    {
      range_list_node_t *const node = list;
      list = node->next;
      free_block(node, &ndx->free_list);
    }
}


/* Based on the data in NDX, build a list of ranges that cover
   [OFFSET, LIMIT) in the "virtual" source data.
   NOTE: The range index must be splayed to OFFSET! */

static range_list_node_t *
build_range_list (apr_off_t offset, apr_off_t limit, range_index_t *ndx)
{
  range_list_node_t *range_list = NULL;
  range_list_node_t *last_range = NULL;
  range_index_node_t *node = ndx->tree;

  while (offset <= limit)
    {
      if (node == NULL)
        return alloc_range_list(&range_list, &last_range, ndx,
                                range_from_source,
                                offset, limit, -1);

      if (offset < node->offset)
        {
          if (limit < node->offset)
            return alloc_range_list(&range_list, &last_range, ndx,
                                    range_from_source,
                                    offset, limit, -1);
          else
            {
              alloc_range_list(&range_list, &last_range, ndx,
                               range_from_source,
                               offset, node->offset - 1, -1);
              offset = node->offset;
            }
        }
      else
        {
          if (offset > node->limit)
            node = node->next;
          else
            {
              if (limit <= node->limit)
                return alloc_range_list(&range_list, &last_range, ndx,
                                        range_from_target,
                                        offset, limit, node->target_offset);
              else
                {
                  alloc_range_list(&range_list, &last_range, ndx,
                                   range_from_target,
                                   offset, node->limit, node->target_offset);
                  offset = node->limit + 1;
                  node = node->next;
                }
            }
        }
    }

  assert(!"This can't happen");
  return range_list;
}


static void
copy_source_ops (svn_txdelta__ops_baton_t *build_baton,
                 const range_list_node_t *range,
                 const svn_txdelta_window_t *window,
                 const offset_index_t *offset_index,
                 apr_pool_t *op_pool, apr_pool_t *pool)
{

  /* FIXME: "Use" unused variables and functions. */
  (void) build_baton;
  (void) range;
  (void) window;
  (void) offset_index;
  (void) op_pool;
  (void) pool;

  (void) search_offset_index;
}



/* ==================================================================== */
/* Bringing it all together. */

/* Trivial optimisation: if the second delta window contains only
   target copies and new data, then it doesn't refer to the source and
   is already the composite. */
static svn_boolean_t
trivial_composition_p (const svn_txdelta_window_t *window)
{
  int i;
  for (i = 0; i < window->num_ops; ++i)
    if (window->ops[i].action_code == svn_txdelta_source)
      return FALSE;
  return TRUE;
}

svn_txdelta_window_t *
svn_txdelta__compose_windows (const svn_txdelta_window_t *window_A,
                              const svn_txdelta_window_t *window_B,
                              apr_pool_t *pool)
{
  if (trivial_composition_p(window_B))
    return NULL;
  else
    {
      apr_pool_t *subpool = svn_pool_create(pool);
      offset_index_t *offset_index = create_offset_index(window_A, subpool);
      range_index_t *range_index = create_range_index(subpool);
      svn_txdelta__ops_baton_t build_baton = { 0 };
      svn_txdelta_window_t *composite = NULL;

      /* FIXME: const char *new_data_A = window_A->new_data->data; */
      const char *new_data_B = window_B->new_data->data;
      apr_size_t target_offset = 0;
      int i;

      /* Read the description of the delta composition algorithm in
         notes/fs-improvements.txt before going any further.
         You have been warned. */
      for (i = 0; i < window_B->num_ops; ++i)
        {
          const svn_txdelta_op_t *op = &window_B->ops[i];
          if (op->action_code != svn_txdelta_source)
            {
              /* Delta ops that don't depend on the source can be copied
                 to the composite unchanged. */
              svn_txdelta__insert_op(&build_baton, op->action_code,
                                     op->offset, op->length,
                                     new_data_B, pool);
              if (op->action_code == svn_txdelta_new)
                new_data_B += op->length;
            }
          else
            {
              /* NOTE: Remember that `offset' and `limit' refer to
                 positions in window_B's _source_ stream, which is the
                 same as window_A's _target_ stream! */
              const apr_off_t offset = op->offset;
              const apr_off_t limit = op->offset + op->length - 1;
              range_list_node_t *range_list, *range;

              splay_range_index(offset, range_index);
              range_list = build_range_list(offset, limit, range_index);

              for (range = range_list; range; range = range->next)
                {
                  if (range->kind == range_from_target)
                    svn_txdelta__insert_op(&build_baton, svn_txdelta_target,
                                           range->target_offset,
                                           range->limit - range->offset + 1,
                                           NULL, pool);
                  else
                    copy_source_ops(&build_baton, range,
                                    window_A, offset_index, pool, subpool);
                }

              free_range_list(range_list, range_index);
              insert_range(offset, limit, target_offset, range_index);
            }

          /* Remember the new offset in the would-be target stream. */
          target_offset += op->length;
        }

      svn_pool_destroy(subpool);
      composite = svn_txdelta__make_window(&build_baton, pool);
      composite->sview_offset = window_A->sview_offset;
      composite->sview_len = window_A->sview_len;
      composite->tview_len = window_B->tview_len;
      return composite;
    }
}



/*
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
