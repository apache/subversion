/*
 * diff.c :  routines for doing diffs
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


#include <apr.h>
#include <apr_pools.h>
#include <apr_general.h>

#include "svn_error.h"
#include "svn_diff.h"
#include "svn_types.h"


typedef struct svn_diff__node_t svn_diff__node_t;
typedef struct svn_diff__tree_t svn_diff__tree_t;
typedef struct svn_diff__position_t svn_diff__position_t;
typedef struct svn_diff__lcs_t svn_diff__lcs_t;

typedef enum svn_diff__type_e
{
  svn_diff__type_common,
  svn_diff__type_diff_modified,
  svn_diff__type_diff_latest,
  svn_diff__type_diff_common,
  svn_diff__type_conflict
} svn_diff__type_e;

struct svn_diff_t {
  svn_diff_t *next;
  svn_diff__type_e type;
  apr_off_t original_start;
  apr_off_t original_length;
  apr_off_t modified_start;
  apr_off_t modified_length;
  apr_off_t latest_start;
  apr_off_t latest_length;
};

struct svn_diff__node_t
{
  svn_diff__node_t     *parent;
  svn_diff__node_t     *left;
  svn_diff__node_t     *right;

  void                *token;
  svn_diff__position_t *position[3];
};

struct svn_diff__tree_t
{
  svn_diff__node_t     *root;
  apr_pool_t          *pool;
};

struct svn_diff__position_t
{
  svn_diff__position_t *next;
  svn_diff__position_t *next_in_node;
  svn_diff__node_t     *node;
  apr_off_t            offset;
};

struct svn_diff__lcs_t
{
  svn_diff__lcs_t *next;
  svn_diff__position_t *position[2];
  int refcount;
};


/*
 * Support functions to build a tree of token positions
 */

static
apr_status_t
svn_diff__tree_create(svn_diff__tree_t **tree, apr_pool_t *pool)
{
  svn_diff__tree_t *newtree;

  *tree = NULL;

  newtree = apr_palloc(pool, sizeof(svn_diff__tree_t));
  newtree->pool = pool;
  newtree->root = NULL;

  *tree = newtree;

  return APR_SUCCESS;
}

static
svn_diff__position_t *
svn_diff__tree_insert_token(svn_diff__tree_t *tree,
                            void *diff_baton,
                            svn_diff_fns_t *vtable,
                            void *token,
                            apr_off_t offset,
                            int idx)
{
  svn_diff__node_t *node;
  svn_diff__node_t **node_ref;
  svn_diff__node_t *parent;
  svn_diff__position_t *position;
  svn_diff__position_t **position_ref;
  int rv;

  parent = NULL;
  node_ref = &tree->root;

  while (*node_ref != NULL)
    {
      parent = *node_ref;

      rv = vtable->token_compare(diff_baton, parent->token, token);
      if (rv == 0)
        {
          /* Discard the token */
          if (vtable->token_discard != NULL)
            vtable->token_discard(diff_baton, token);

          /* Insert the correct position */
          position_ref = &parent->position[idx];

          while (*position_ref != NULL
                 && (*position_ref)->offset > offset)
            {
              position_ref = &(*position_ref)->next_in_node;
            }

          if (*position_ref != NULL
              && (*position_ref)->offset == offset)
            {
              return NULL;
            }

          /* Create a new position */
          position = apr_palloc(tree->pool, sizeof(svn_diff__position_t));
          position->next = NULL;
          position->next_in_node = *position_ref;
          position->node = parent;
          position->offset = offset;

          *position_ref = position;
          return position;
        }
      else if (rv > 0)
        {
          node_ref = &parent->left;
        }
      else
        {
          node_ref = &parent->right;
        }
    }

  /* Create a new node */
  node = apr_palloc(tree->pool, sizeof(*node));
  node->parent = parent;
  node->left = NULL;
  node->right = NULL;
  node->token = token;
  node->position[0] = NULL;
  node->position[1] = NULL;

  *node_ref = node;

  /* Create a new position */
  position = apr_palloc(tree->pool, sizeof(svn_diff__position_t));
  position->next = NULL;
  position->next_in_node = NULL;
  position->node = node;
  position->offset = offset;

  node->position[idx] = position;

  return position;
}

static
int
svn_diff__tree_compare_common_token_count(svn_diff__node_t *node,
                                          int idx1, int idx2)
{
  int count = 0;
  svn_diff__position_t *position;

  if (node->position[idx1] != NULL && node->position[idx2] != NULL)
    {
      position = node->position[idx1];
      while (position)
        {
          count++;
          position = position->next_in_node;
        }

      position = node->position[idx2];
      while (position)
        {
          count--;
          position = position->next_in_node;
        }
    }

    if (node->left != NULL)
      {
        count += svn_diff__tree_compare_common_token_count(node->left, idx1, idx2);
      }

    if (node->right != NULL)
      {
        count += svn_diff__tree_compare_common_token_count(node->right, idx1, idx2);
      }

  return count;
}

static
int
svn_diff__tree_largest_common_alphabet_user(svn_diff__tree_t *tree, int idx1, int idx2)
{
  if (tree->root == NULL)
    return -1;

  return svn_diff__tree_compare_common_token_count(tree->root, idx1, idx2) > 0 ? idx1 : idx2;
}


/*
 * Support function to reverse a linked list; in this case, the the LCS.
 */

static
svn_diff__lcs_t *
svn_diff__lcs_reverse(svn_diff__lcs_t *lcs)
{
  svn_diff__lcs_t *next;
  svn_diff__lcs_t *prev;

  next = NULL;
  while (lcs != NULL)
    {
      prev = lcs->next;
      lcs->next = next;
      next = lcs;
      lcs = prev;
    }

  return next;
}


/*
 * Support structure to implement 'large arrays'.
 */

typedef struct svn_diff__hat_t svn_diff__hat_t;

struct svn_diff__hat_t
{
  svn_diff__hat_t   *next;
  apr_pool_t       *pool;
  svn_diff__lcs_t *links[1];
};

static
apr_status_t
svn_diff__hat_create(svn_diff__hat_t **hat, apr_pool_t *pool)
{
    svn_diff__hat_t *newhat;

    *hat = NULL;

    newhat = apr_pcalloc(pool, sizeof(svn_diff__hat_t) + 4095 * sizeof(svn_diff__lcs_t *));
    newhat->pool = pool;

    *hat = newhat;

    return APR_SUCCESS;
}

static
void
svn_diff__hat_set(svn_diff__hat_t *hat, apr_size_t idx, svn_diff__lcs_t *link)
{
  apr_size_t size;
  svn_diff__hat_t *newhat;

  size = 4096;
  while (idx > size
         && hat->next != NULL)
    {
      idx -= size;
      size <<= 1;
      hat = hat->next;
    }

  while (idx > size)
    {
      newhat = apr_pcalloc(hat->pool,
                           sizeof(svn_diff__hat_t)
                           + (size - 1) * sizeof(svn_diff__lcs_t *));
      newhat->pool = hat->pool;

      hat->next = newhat;
      hat = hat->next;

      idx -= size;
      size <<= 1;
    }

  hat->links[idx] = link;
}

static
svn_diff__lcs_t *
svn_diff__hat_get(svn_diff__hat_t *hat, apr_size_t idx)
{
  apr_size_t size;

  size = 4096;
  while (idx > size
         && hat->next != NULL)
    {
      idx -= size;
      size <<= 1;
      hat = hat->next;
    }

  if (hat == NULL)
    {
        return NULL;
    }

  return hat->links[idx];
}


/*
 * Actual diff routines
 */

static
apr_status_t
svn_diff__get_tokens(svn_diff__position_t **position_list,
                     svn_diff__tree_t *tree,
                     void *diff_baton,
                     svn_diff_fns_t *vtable,
                     svn_diff_datasource_e datasource,
                     int position_idx)
{
  svn_diff__position_t *position;
  svn_diff__position_t **position_ref;
  void *token;
  apr_off_t offset;
  apr_status_t rv;

  *position_list = NULL;
  
  rv = vtable->datasource_open(diff_baton, datasource);
  if (rv != APR_SUCCESS)
    return rv;
  
  position_ref = &position;
  offset = 0;
  while (1)
    {
      token = vtable->datasource_get_next_token(diff_baton, datasource);
      if (token == NULL)
        break;

      offset++;
      *position_ref = svn_diff__tree_insert_token(tree, diff_baton, vtable, token, offset, position_idx);

      position_ref = &(*position_ref)->next;
    }

  *position_ref = NULL;
  
  vtable->datasource_close(diff_baton, datasource);

  *position_list = position;
  
  return APR_SUCCESS;
}

static
svn_diff__lcs_t *
svn_diff__lcs(svn_diff__tree_t *tree,
              svn_diff__position_t *position_list1,
              svn_diff__position_t *position_list2,
              int idx1, int idx2,
              apr_pool_t *pool)
{
  svn_diff__node_t *node;
  svn_diff__position_t *position[2];
  svn_diff__position_t *position_list[2];
  svn_diff__lcs_t *link;
  svn_diff__hat_t *hat;
  int idx;
  apr_size_t k, t;
  apr_size_t middle, range, offset;
  svn_diff__lcs_t **ref, *freelist;
  svn_diff__lcs_t sentinel_link;
  svn_diff__position_t sentinel_position[2];

  position_list[0] = position_list1;
  position_list[1] = position_list2;
  
  idx = svn_diff__tree_largest_common_alphabet_user(tree, idx1, idx2) == idx1 ? 0 : 1;

  svn_diff__hat_create(&hat, tree->pool);

  sentinel_position[0].next = NULL;
  sentinel_position[0].offset = 0;
  sentinel_position[0].node = NULL;
  sentinel_position[1].next = NULL;
  sentinel_position[1].offset = 0;
  sentinel_position[1].node = NULL;

  sentinel_link.next = NULL;
  sentinel_link.position[0] = &sentinel_position[0];
  sentinel_link.position[1] = &sentinel_position[1];
  sentinel_link.refcount = 1;

  svn_diff__hat_set(hat, 0, &sentinel_link);

  freelist = NULL;
  t = 1;

  position[idx] = position_list[idx];
  while (position[idx] != NULL)
    {
      node = position[idx]->node;

      position[1 - idx] = node->position[1 - idx];
      while (position[1 - idx] != NULL)
        {
          range = t;
          offset = 0;
          middle = range >> 1;
          k = offset + middle;
          while (range > 0)
            {
              link = svn_diff__hat_get(hat, k);
              if (link->position[1 - idx]->offset == position[1 - idx]->offset)
                {
                  break;
                }

              if (link->position[1 - idx]->offset > position[1 - idx]->offset)
                {
                  range = middle;
                }
              else
                {
                  middle++;
                  offset += middle;
                  range -= middle;
                }

              middle = range >> 1;
              k = offset + middle;
              link = NULL;
            }

          if (link == NULL
              || link->position[1 - idx]->offset < position[1 - idx]->offset)
            {
              if (link == NULL)
                {
                  link = freelist;
                }
              else
                {
                  ref = &link;
                  while (*ref != NULL)
                    {
                      (*ref)->refcount--;

                      if ((*ref)->refcount > 0)
                        {
                          *ref = freelist;
                          break;
                        }

                      ref = &(*ref)->next;
                    }
                }

              if (link != NULL)
                {
                  freelist = link->next;
                }
              else
                {
                  link = apr_palloc(tree->pool, sizeof(*link));
                }

              link->next = svn_diff__hat_get(hat, k - 1);
              link->next->refcount++;
              link->position[0] = position[0];
              link->position[1] = position[1];
              link->refcount = 1;
              svn_diff__hat_set(hat, k, link);

              if (k >= t)
                t = k + 1;
            }

          position[1 - idx] = position[1 - idx]->next_in_node;
        }

      position[idx] = position[idx]->next;
    }

  /* Tack on an EOF link, since that is always a sync point */
  link = freelist;
  if (link == NULL)
    {
      link = apr_palloc(tree->pool, sizeof(*link));
    }
  
  link->next = svn_diff__hat_get(hat, t - 1);
  link->position[0] = NULL;
  link->position[1] = NULL; 
  
  /* reverse the list */
  link = svn_diff__lcs_reverse(link);

  /* Remove the sentinel from the lcs */
  link = link->next;

  return link;
}

apr_status_t
svn_diff(svn_diff_t **diff,
         void *diff_baton,
         svn_diff_fns_t *vtable,
         apr_pool_t *pool)
{
  svn_diff__tree_t *tree;
  svn_diff__position_t *position_list[2];
  apr_pool_t *subpool;
  apr_status_t rv;
  svn_diff__lcs_t *lcs;

  *diff = NULL;

  rv = apr_pool_create(&subpool, pool);
  if (rv != APR_SUCCESS)
    return rv;

  rv = svn_diff__tree_create(&tree,
                             subpool);
  if (rv != APR_SUCCESS)
    return rv;

  /* Insert the data into the tree */
  rv = svn_diff__get_tokens(&position_list[0],
                            tree,
                            diff_baton, vtable,
                            svn_diff_datasource_original, 0);
  if (rv != APR_SUCCESS)
    return rv;
  
  rv = svn_diff__get_tokens(&position_list[1],
                            tree,
                            diff_baton, vtable,
                            svn_diff_datasource_modified, 1);
  if (rv != APR_SUCCESS)
    return rv;


  /* The cool part is that we don't need the tokens anymore.
   * Allow the app to clean them up if it wants to.
   */
  if (vtable->token_discard_all != NULL)
    vtable->token_discard_all(diff_baton);

  /* Get the lcs */
  lcs = svn_diff__lcs(tree,
                      position_list[0],
                      position_list[1],
                      0, 1,
                      subpool);

  /* ### We don't need the nodes in the tree either anymore.  However,
   * ### the tree pool is also used for the positions, so we can't get
   * ### rid of them.  Split this later.
   */
  
  /* Produce a diff */
  {
    apr_off_t original_start;
    apr_off_t modified_start;
    apr_off_t original_length;
    apr_off_t modified_length;
    apr_off_t common_length;

    svn_diff_t **diff_ref = diff;
    
    svn_diff__position_t *position[2];

    position[0] = position_list[0];
    position[1] = position_list[1];
    
    original_start = 0;
    modified_start = 0;
    do
      {
        common_length = 0;

        /* Determine length of common block */
        while (lcs->next != NULL
               && position[0] == lcs->position[0]
               && position[1] == lcs->position[1])
          {
            common_length++;

            position[0] = position[0]->next;
            position[1] = position[1]->next;

            lcs = lcs->next;
          }

        /* Output the common block if there is one */
        if (common_length > 0)
          {
            (*diff_ref) = apr_palloc(pool, sizeof(**diff_ref));

            (*diff_ref)->type = svn_diff__type_common;
            (*diff_ref)->original_start = original_start;
            (*diff_ref)->original_length = common_length;
            (*diff_ref)->modified_start = modified_start;
            (*diff_ref)->modified_length = common_length;
            (*diff_ref)->latest_start = 0;
            (*diff_ref)->latest_length = 0;

            diff_ref = &(*diff_ref)->next;

            /* Set the new offsets */
            original_start += common_length;
            modified_start += common_length;
          }

        original_length = 0;
        modified_length = 0;

        while (position[0] != lcs->position[0])
          {
            original_length++;

            position[0] = position[0]->next;
          }

        while (position[1] != lcs->position[1])
          {
            modified_length++;

            position[1] = position[1]->next;
          }

        if (original_length > 0 || modified_length > 0)
          {
            (*diff_ref) = apr_palloc(pool, sizeof(**diff_ref));

            (*diff_ref)->type = svn_diff__type_diff_modified;
            (*diff_ref)->original_start = original_start;
            (*diff_ref)->original_length = original_length;
            (*diff_ref)->modified_start = modified_start;
            (*diff_ref)->modified_length = modified_length;
            (*diff_ref)->latest_start = 0;
            (*diff_ref)->latest_length = 0;

            diff_ref = &(*diff_ref)->next;

            /* Set the new offsets */
            original_start += original_length;
            modified_start += modified_length;
          }
      }
    while (lcs->next != NULL);
          
    *diff_ref = NULL;
  }
  
  /* Get rid of all the data we don't have a use for anymore */
  apr_pool_destroy(subpool);

  return APR_SUCCESS;
}

apr_status_t
svn_diff3(svn_diff_t **diff,
          void *diff_baton,
          svn_diff_fns_t *vtable,
          apr_pool_t *pool)
{
  apr_pool_t *subpool;
  svn_diff__tree_t *tree;
  svn_diff__position_t *position_list[3];
  svn_diff__lcs_t *lcs_bw;
  svn_diff__lcs_t *lcs_br;
  apr_status_t rv;

  *diff = NULL;

  rv = apr_pool_create(&subpool, pool);
  if (rv != APR_SUCCESS)
    return rv;

  rv = svn_diff__tree_create(&tree, subpool);
  if (rv != APR_SUCCESS)
    return rv;

  rv = svn_diff__get_tokens(&position_list[0], 
                            tree, 
                            diff_baton, vtable,
                            svn_diff_datasource_original, 0);
  if (rv != APR_SUCCESS)
    return rv;

  rv = svn_diff__get_tokens(&position_list[1],
                            tree,
                            diff_baton, vtable,
                            svn_diff_datasource_modified, 1);
  if (rv != APR_SUCCESS)
    return rv;
  
  rv = svn_diff__get_tokens(&position_list[2],
                            tree,
                            diff_baton, vtable,
                            svn_diff_datasource_latest, 2);
  if (rv != APR_SUCCESS)
    return rv;
  
  /* Get rid of the tokens, we don't need them to calc the diff */
  if (vtable->token_discard_all != NULL)
    vtable->token_discard_all(diff_baton);

  /* Get the lcs for original-modified and original-latest */
  lcs_bw = svn_diff__lcs(tree, 
                         position_list[0], position_list[1], 
                         0, 1, 
                         subpool);
  lcs_br = svn_diff__lcs(tree,
                         position_list[0], position_list[2],
                         0, 2,
                         subpool);

  /* ### We don't need the nodes in the tree either anymore.  However,
   * ### the tree pool is also used for the positions, so we can't get
   * ### rid of them.  Split this later.
   */
  
  /* Produce a merged diff */
  {
    apr_off_t original_start;
    apr_off_t modified_start;
    apr_off_t latest_start;
    apr_off_t original_length;
    apr_off_t modified_length;
    apr_off_t latest_length;
    apr_off_t common_length;

    svn_diff__lcs_t *sync_lcs_bw;
    svn_diff__lcs_t *sync_lcs_br;

    svn_diff__position_t *position[3];
    svn_diff__position_t *sync_position[3];

    svn_boolean_t diff_modified;
    svn_boolean_t diff_latest;

    svn_diff_t **diff_ref = diff;

    svn_diff__type_e type;

    position[0] = position_list[0];
    position[1] = position_list[1];
    position[2] = position_list[2];
    
    original_start = 0;
    modified_start = 0;
    latest_start = 0;
    do
      {
        common_length = 0;

        /* Determine length of common block */
        while (lcs_bw->next != NULL && lcs_br->next != NULL
               && position[0] == lcs_bw->position[0]
               && position[0] == lcs_br->position[0]
               && position[1] == lcs_bw->position[1]
               && position[2] == lcs_br->position[1])
          {
            common_length++;

            position[0] = position[0]->next;
            position[1] = position[1]->next;
            position[2] = position[2]->next;

            lcs_bw = lcs_bw->next;
            lcs_br = lcs_br->next;
          }

        /* Output the common block if there is one */
        if (common_length > 0)
          {
            (*diff_ref) = apr_palloc(pool, sizeof(**diff_ref));

            (*diff_ref)->type = svn_diff__type_common;
            (*diff_ref)->original_start = original_start;
            (*diff_ref)->original_length = common_length;
            (*diff_ref)->modified_start = modified_start;
            (*diff_ref)->modified_length = common_length;
            (*diff_ref)->latest_start = latest_start;
            (*diff_ref)->latest_length = common_length;

            diff_ref = &(*diff_ref)->next;

            /* Set the new offsets */
            original_start += common_length;
            modified_start += common_length;
            latest_start += common_length;
          }

        /* Here we can encounter:
         * - diff_modified
         * - diff_latest
         * - diff_common 
         * - conflict
         */

        original_length = 0;
        modified_length = 0;
        latest_length = 0;

        /* Save the modified and latest positions */
        sync_position[0] = position[0];
        sync_position[1] = position[1];
        sync_position[2] = position[2];

        diff_modified = FALSE;
        diff_latest = FALSE;
        
        sync_lcs_bw = lcs_bw;
        sync_lcs_br = lcs_br;

        /* Find the next sync point.  There should always be
         * one present, since EOF is a sync point.
         */
        while (sync_lcs_bw->position[0] != sync_lcs_br->position[0])
          {   
            if (position[0] == sync_lcs_bw->position[0])
              {
                sync_lcs_bw = sync_lcs_bw->next;
              }
            else
              {
                diff_modified = TRUE;
              }

            if (position[0] == sync_lcs_br->position[0])
              {
                sync_lcs_br = sync_lcs_br->next;
              }
            else
              {
                diff_latest = TRUE;
              }

            original_length++;

            position[0] = position[0]->next;
          }

        while (position[0] != sync_lcs_bw->position[0])
          {
            original_length++;

            position[0] = position[0]->next;
          }

        while (position[1] != sync_lcs_bw->position[1])
          {
            if (position[1] != lcs_bw->position[1])
              {
                diff_modified = TRUE;

                do
                  {
                    modified_length++;

                    position[1] = position[1]->next;
                  }
                while (position[1] != lcs_bw->position[1]);
              }
            else
              {
                modified_length++;
                    
                position[1] = position[1]->next;
                lcs_bw = lcs_bw->next;
              }
          }

        while (position[2] != sync_lcs_br->position[1])
          {
            if (position[2] != lcs_br->position[1])
              {
                diff_latest = TRUE;

                do
                  {
                    latest_length++;

                    position[2] = position[2]->next;
                  }
                while (position[2] != lcs_br->position[1]);
              }
            else
              {
                latest_length++;
                    
                position[2] = position[2]->next;
                lcs_br = lcs_br->next;
              }
          }

        while (lcs_br != sync_lcs_br)
          {
            if (position[2] != lcs_br->position[1])
              {
                diff_latest = TRUE;
                
                do
                  {
                    latest_length++;

                    position[2] = position[2]->next;
                  }
                while (position[2] != lcs_br->position[1]);
              }
            
            latest_length++;

            position[2] = position[2]->next;

            lcs_br = lcs_br->next;
          }

        if (original_length > 0
            || modified_length > 0
            || latest_length > 0)
          {
            if (diff_modified && !diff_latest)
              {
                type = svn_diff__type_diff_modified;
              }
            else if (!diff_modified && diff_latest)
              {
                type = svn_diff__type_diff_latest;
              }
            else
              {
                /* We can detect common/conflict here */
                type = svn_diff__type_diff_common;

                if (modified_length != latest_length)
                  {
                    type = svn_diff__type_conflict;
                  }
                else
                  {
                    while (sync_position[1] != position[1]
                           && sync_position[2] != position[2])
                      {
                        if (sync_position[1]->node != sync_position[2]->node)
                          {
                            type = svn_diff__type_conflict;
                            break;
                          }

                        sync_position[1] = sync_position[1]->next;
                        sync_position[2] = sync_position[2]->next;
                      }
                  }

                /* ### If we have a conflict we can try to find the
                 * ### common parts in it by getting an lcs between
                 * ### modified (start to start + length) and
                 * ### latest (start to start + length).
                 * ### We use this lcs to create a simple diff.  Only
                 * ### where there is a diff between the two, we have
                 * ### a conflict.
                 * ### This raises a problem; several common diffs and
                 * ### conflicts can occur within the same original block.
                 * ### This needs some thought.
                 * ###
                 * ### NB: We can use the node _pointers_ to identify
                 * ###     different tokens
                 */
              }

            (*diff_ref) = apr_palloc(pool, sizeof(**diff_ref));

            (*diff_ref)->type = type;
            (*diff_ref)->original_start = original_start;
            (*diff_ref)->original_length = original_length;
            (*diff_ref)->modified_start = modified_start;
            (*diff_ref)->modified_length = modified_length;
            (*diff_ref)->latest_start = latest_start;
            (*diff_ref)->latest_length = latest_length;

            diff_ref = &(*diff_ref)->next;

            /* Set the new offsets */
            original_start += original_length;
            modified_start += modified_length;
            latest_start += latest_length;
          }
      }
    while (lcs_bw->next != NULL && lcs_br->next != NULL);
          
    *diff_ref = NULL;
  }

  apr_pool_destroy(subpool);

  return APR_SUCCESS;
}


svn_boolean_t
svn_diff_contains_conflicts(svn_diff_t *diff)
{
  while (diff != NULL)
    {
      if (diff->type == svn_diff__type_conflict)
        {
          return TRUE;
        }

      diff = diff->next;
    }

  return FALSE;
}

svn_boolean_t
svn_diff_contains_diffs(svn_diff_t *diff)
{
  while (diff != NULL)
    {
      if (diff->type != svn_diff__type_common)
        {
          return TRUE;
        }

      diff = diff->next;
    }

  return FALSE;
}

void
svn_diff_output(svn_diff_t *diff,
                void *output_baton,
                svn_diff_output_fns_t *vtable)
{
  while (diff != NULL)
    {
      switch (diff->type)
        {
          case svn_diff__type_common:
            if (vtable->output_common != NULL)
              {
                vtable->output_common(output_baton,
                                      diff->original_start,
                                      diff->original_length,
                                      diff->modified_start,
                                      diff->modified_length,
                                      diff->latest_start,
                                      diff->latest_length);
              }
            break;

        case svn_diff__type_diff_common:
            if (vtable->output_diff_common != NULL)
              {
                vtable->output_diff_common(output_baton,
                                           diff->original_start,
                                           diff->original_length,
                                           diff->modified_start,
                                           diff->modified_length,
                                           diff->latest_start,
                                           diff->latest_length);
              }
            break;

        case svn_diff__type_diff_modified:
            if (vtable->output_diff_modified != NULL)
              {
                vtable->output_diff_modified(output_baton,
                                                diff->original_start,
                                                diff->original_length,
                                                diff->modified_start,
                                                diff->modified_length,
                                                diff->latest_start,
                                                diff->latest_length);
              }
            break;

        case svn_diff__type_diff_latest:
            if (vtable->output_diff_latest != NULL)
              {
                vtable->output_diff_latest(output_baton,
                                               diff->original_start,
                                               diff->original_length,
                                               diff->modified_start,
                                               diff->modified_length,
                                               diff->latest_start,
                                               diff->latest_length);
              }
            break;

        case svn_diff__type_conflict:
            if (vtable->output_conflict != NULL)
              {
                vtable->output_conflict(output_baton,
                                        diff->original_start,
                                        diff->original_length,
                                        diff->modified_start,
                                        diff->modified_length,
                                        diff->latest_start,
                                        diff->latest_length);
              }
            break;
        }

      diff = diff->next;
    }
}
