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
/* Mapping offsets in the target streem to txdelta ops. */

typedef struct offset_index_t
{
  int length;
  apr_off_t *offs;
} offset_index_t;

/* Create an index mapping target stream offsets to delta ops in
   WINDOW. Allocate from POOL. */

static offset_index_t *
create_offset_index (const svn_txdelta_window_t *window,
                     apr_pool_t *pool)
{
  offset_index_t *ndx = apr_palloc(pool, sizeof(*ndx));
  apr_off_t offset = 0;
  int i;

  ndx->length = window->num_ops;
  ndx->offs = apr_palloc(pool, (ndx->length + 1) * sizeof(*ndx->offs));

  for (i = 0; i < ndx->length; ++i)
    {
      ndx->offs[i] = offset;
      offset += window->ops[i].length;
    }
  ndx->offs[ndx->length] = offset;

  return ndx;
}

/* Find the index of the delta op thet defines that data at OFFSET in
   NDX. */

static int
search_offset_index (offset_index_t *ndx, apr_off_t offset)
{
  int lo, hi, op;

  assert(offset >= 0);
  assert(offset < ndx->offs[ndx->length]);

  for (lo = 0, hi = ndx->length, op = (lo + hi)/2;
       lo < hi;
       op = (lo + hi)/2)
    {
      const apr_off_t this_offset = ndx->offs[op];
      const apr_off_t next_offset = ndx->offs[op + 1];
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

  assert(ndx->offs[op] <= offset && offset < ndx->offs[op + 1]);
  return op;
}



/* ==================================================================== */
/* Mapping ranges in the source stream to ranges in the composed delta. */

typedef struct range_index_node_t range_index_node_t;
struct range_index_node_t
{
  apr_off_t offset;
  apr_off_t limit;
  apr_off_t target_offset;
  range_index_node_t *left, *right;
  range_index_node_t *prev, *next;
};

typedef struct range_index_t
{
  range_index_node_t *tree;
  range_index_node_t *free_list;
  apr_pool_t *pool;
} range_index_t;

/* Create a range index tree. Allocate from POOL. */
static range_index_t *
create_range_index (apr_pool_t *pool)
{
  range_index_t *ndx = apr_palloc(pool, sizeof(*ndx));
  ndx->tree = ndx->free_list = NULL;
  ndx->pool = pool;
  return ndx;
}

/* Allocate a node for the range index tree. */
static APR_INLINE range_index_node_t *
alloc_range_index_node (range_index_t *ndx,
                        apr_off_t offset,
                        apr_off_t limit,
                        apr_off_t target_offset)
{
  range_index_node_t *node;
  if (ndx->free_list == NULL)
    node = apr_palloc(ndx->pool, sizeof(*node));
  else
    {
      node = ndx->free_list;
      ndx->free_list = node->right;
    }

  node->offset = offset;
  node->limit = limit;
  node->target_offset = target_offset;
  node->left = node->right = NULL;
  node->prev = node->next = NULL;
  return node;
}

/* Free a node from the range index tree. */
static APR_INLINE void
free_range_index_node (range_index_t *ndx, range_index_node_t *node)
{
  node->right = ndx->free_list;
  ndx->free_list = node;
}

/* Splay the index tree, using OFFSET as the key. */

static range_index_node_t *
splay_range_index (apr_off_t offset, range_index_node_t *tree)
{
  range_index_node_t scratch_node;
  range_index_node_t *left, *right;

  if (tree == NULL)
    return NULL;

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
  left->right  = tree->left;
  right->left  = tree->right;
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
          tree->left = node->right; /* Which is always NULL */
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

  return tree;
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


/* Add a range [OFFSET, LIMIT) into NDX. If NDX already contains
   a range that encloses [OFFSET, LIMIT), do nothing. Otherwise,
   remove all ranges from NDX that are superseded by the new
   range. */

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
      ndx->tree = splay_range_index(offset, ndx->tree);

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

          svn_boolean_t insert_range_p = FALSE;
          svn_boolean_t left_overrides_p = FALSE;

          if (ndx->tree->right != NULL)
            {
              node = ndx->tree->right;
              while (node->left != NULL)
                node = node->left;

              insert_range_p = (ndx->tree->limit < node->offset
                                || limit > node->limit);
            }

          if (insert_range_p)
            {
              /* Again, we have to check if the new node in and the one
                 to the left of the root override root's range. */

              /* FIXME: splay_range_index should return a pointer to this
                 node, because it does the exact same traversal before
                 the giant promotion. */
              if (ndx->tree->left != NULL)
                {
                  node = ndx->tree->left;
                  while (node->right != NULL)
                    node = node->right;

                  left_overrides_p = (node->limit >= offset);
                }

              if (left_overrides_p)
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
          node->left = NULL;
          node->right = ndx->tree;
          ndx->tree = node;
          clean_tree(ndx, limit);
        }
      else
        /* Ignore the range */;
    }
}



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

      const char *new_data_A = window_A->new_data->data;
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
              /* ToDo: replace with data from window_A. */

              /* ### this is the _last_ thing to do! */
              insert_range(op->offset, op->offset + op->length - 1,
                           target_offset, range_index);
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
