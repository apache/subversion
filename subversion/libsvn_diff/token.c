/*
 * token.c :  routines for doing diffs
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */


#include <apr.h>
#include <apr_pools.h>
#include <apr_general.h>

#include "svn_error.h"
#include "svn_diff.h"
#include "svn_types.h"

#include "diff.h"


/*
 * Prime number to use as the size of the hash table.  This number was
 * not selected by testing of any kind and may need tweaking.
 */
#define SVN_DIFF__HASH_SIZE 127

struct svn_diff__node_t
{
  svn_diff__node_t     *parent;
  svn_diff__node_t     *left;
  svn_diff__node_t     *right;

  apr_uint32_t          hash;
  void                 *token;
};

struct svn_diff__tree_t
{
  svn_diff__node_t     *root[SVN_DIFF__HASH_SIZE];
  apr_pool_t           *pool;
};


/*
 * Support functions to build a tree of token positions
 */

void
svn_diff__tree_create(svn_diff__tree_t **tree, apr_pool_t *pool)
{
  *tree = apr_pcalloc(pool, sizeof(**tree));
  (*tree)->pool = pool;
}


static svn_error_t *
svn_diff__tree_insert_token(svn_diff__node_t **node, svn_diff__tree_t *tree,
                            void *diff_baton,
                            const svn_diff_fns_t *vtable,
                            apr_uint32_t hash, void *token)
{
  svn_diff__node_t *new_node;
  svn_diff__node_t **node_ref;
  svn_diff__node_t *parent;
  int rv;

  SVN_ERR_ASSERT(token);

  parent = NULL;
  node_ref = &tree->root[hash % SVN_DIFF__HASH_SIZE];

  while (*node_ref != NULL)
    {
      parent = *node_ref;

      rv = hash - parent->hash;
      if (!rv)
        SVN_ERR(vtable->token_compare(diff_baton, parent->token, token, &rv));

      if (rv == 0)
        {
          /* Discard the previous token.  This helps in cases where
           * only recently read tokens are still in memory.
           */
          if (vtable->token_discard != NULL)
            vtable->token_discard(diff_baton, parent->token);

          parent->token = token;
          *node = parent;

          return SVN_NO_ERROR;
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
  new_node = apr_palloc(tree->pool, sizeof(*new_node));
  new_node->parent = parent;
  new_node->left = NULL;
  new_node->right = NULL;
  new_node->hash = hash;
  new_node->token = token;

  *node = *node_ref = new_node;

  return SVN_NO_ERROR;
}


/*
 * Get all tokens from a datasource.  Return the
 * last item in the (circular) list.
 */
svn_error_t *
svn_diff__get_tokens(svn_diff__position_t **position_list,
                     svn_diff__tree_t *tree,
                     void *diff_baton,
                     const svn_diff_fns_t *vtable,
                     svn_diff_datasource_e datasource,
                     apr_pool_t *pool)
{
  svn_diff__position_t *start_position;
  svn_diff__position_t *position = NULL;
  svn_diff__position_t **position_ref;
  svn_diff__node_t *node;
  void *token;
  apr_off_t offset;
  apr_uint32_t hash;

  *position_list = NULL;


  SVN_ERR(vtable->datasource_open(diff_baton, datasource, FALSE));

  position_ref = &start_position;
  offset = 0;
  hash = 0; /* The callback fn doesn't need to touch it per se */
  while (1)
    {
      SVN_ERR(vtable->datasource_get_next_token(&hash, &token,
                                                diff_baton, datasource));
      if (token == NULL)
        break;

      offset++;
      SVN_ERR(svn_diff__tree_insert_token(&node, tree,
                                          diff_baton, vtable,
                                          hash, token));

      /* Create a new position */
      position = apr_palloc(pool, sizeof(*position));
      position->next = NULL;
      position->node = node;
      position->offset = offset;

      *position_ref = position;
      position_ref = &position->next;
    }

  *position_ref = start_position;

  SVN_ERR(vtable->datasource_close(diff_baton, datasource));

  *position_list = position;

  return SVN_NO_ERROR;
}

/* Find identical suffix between all datasources
 */
static svn_error_t *
find_identical_suffix(svn_boolean_t *reached_one_bof,
                      void *diff_baton,
                      const svn_diff_fns_t *vtable,
                      svn_diff_datasource_e datasource[],
                      int datasource_len)
{
  void *token[4];
  svn_boolean_t is_match, reached_all_bof;
  int i, rv;

  *reached_one_bof = FALSE;
  while (1)
    {
      /* Keep getting tokens and matching them, until there are no tokens
         left, or we encounter a non-matching token. */
      for (i = 0; i < datasource_len; i++)
        {
          SVN_ERR(vtable->datasource_get_previous_token(&token[i], diff_baton,
                                                        datasource[i]));
          *reached_one_bof = *reached_one_bof || token[i] == NULL;
        }
      if (*reached_one_bof)
        {
          break;
        }
      else
        {
          for (i = 1, is_match = TRUE; is_match && i < datasource_len; i++)
            {
              SVN_ERR(vtable->token_compare(diff_baton, token[0], token[i], &rv));
              is_match = is_match && rv == 0;
            }
          if (!is_match)
            break;
        }
    }

  /* If all files reached their beginning (i.e. are fully identical),
     we're done. */
  for (i = 0, reached_all_bof = TRUE; i < datasource_len; i++)
    reached_all_bof = reached_all_bof && token[i] == NULL;
  if (reached_all_bof)
    return SVN_NO_ERROR;

  /* Push back the non-matching token we read. */
  for (i = 0; i < datasource_len; i++)
    if (token[i] != NULL)
      SVN_ERR(vtable->token_pushback_suffix(diff_baton, token[i], datasource[i]));

  return SVN_NO_ERROR;
}


/* Find identical prefix between all datasources
 */
static svn_error_t *
find_identical_prefix(apr_off_t *prefix_lines,
                      void *diff_baton,
                      const svn_diff_fns_t *vtable,
                      svn_diff_datasource_e datasource[],
                      int datasource_len)
{
  void *token[4];
  svn_boolean_t is_match, reached_one_eof, reached_all_eof;
  int i, rv;

  *prefix_lines = 0;
  reached_one_eof = FALSE;
  while (1)
    {
      /* Keep getting tokens and matching them, until there are no tokens
         left, or we encounter a non-matching token. */
      for (i = 0; i < datasource_len; i++)
        {
          SVN_ERR(vtable->datasource_get_next_token(NULL, &token[i],
                                                    diff_baton, datasource[i]));
          reached_one_eof = reached_one_eof || token[i] == NULL;
        }
      if (reached_one_eof)
        {
          break;
        }
      else
        {
          for (i = 1, is_match = TRUE; is_match && i < datasource_len; i++)
            {
              SVN_ERR(vtable->token_compare(diff_baton, token[0], token[i], &rv));
              is_match = is_match && rv == 0;
            }
          if (is_match)
            (*prefix_lines)++;
          else
            break;
        }
    }

  /* If all files reached their end (i.e. are fully identical), we're done. */
  for (i = 0, reached_all_eof = TRUE; i < datasource_len; i++)
    reached_all_eof = reached_all_eof && token[i] == NULL;
  if (reached_all_eof)
    return SVN_NO_ERROR;

  /* Push back the non-matching token we read. */
  for (i = 0; i < datasource_len; i++)
    if (token[i] != NULL)
      SVN_ERR(vtable->token_pushback_prefix(diff_baton, token[i], datasource[i]));

  return SVN_NO_ERROR;
}

/*
 * Get all tokens from the datasources.  For each datasource, return the
 * last item in the (circular) list.
 */
svn_error_t *
svn_diff__get_all_tokens(svn_diff__position_t **position_list[],
                         apr_off_t *prefix_lines,
                         svn_diff__tree_t *tree,
                         void *diff_baton,
                         const svn_diff_fns_t *vtable,
                         svn_diff_datasource_e datasource[],
                         int datasource_len,
                         apr_pool_t *pool)
{
  svn_diff__position_t *start_position;
  svn_diff__position_t *position = NULL;
  svn_diff__position_t **position_ref;
  svn_diff__node_t *node;
  void *token;
  apr_off_t offset;
  apr_uint32_t hash;
  svn_boolean_t reached_one_bof;
  int i;

  for (i = 0; i < datasource_len; i++)
    {
      *position_list[i] = NULL;
      SVN_ERR(vtable->datasource_open(diff_baton, datasource[i], TRUE));
    }

  /* find identical suffix */
  SVN_ERR(find_identical_suffix(&reached_one_bof, diff_baton, vtable,
                                datasource, datasource_len));

  for (i = 0; i < datasource_len; i++)
    {
      SVN_ERR(vtable->datasource_open(diff_baton, datasource[i], FALSE));
    }

  /* find identical prefix (but don't bother if one file was all suffix) */
  /*if (!reached_one_bof)*/
    SVN_ERR(find_identical_prefix(prefix_lines, diff_baton, vtable,
                                  datasource, datasource_len));

  for (i = 0; i < datasource_len; i++)
    {
      position_ref = &start_position;
      offset = *prefix_lines;
      hash = 0; /* The callback fn doesn't need to touch it per se */
      while (1)
        {
          SVN_ERR(vtable->datasource_get_next_token(&hash, &token,
                                                    diff_baton, datasource[i]));
          if (token == NULL)
            break;

          offset++;
          SVN_ERR(svn_diff__tree_insert_token(&node, tree,
                                              diff_baton, vtable,
                                              hash, token));

          /* Create a new position */
          position = apr_palloc(pool, sizeof(*position));
          position->next = NULL;
          position->node = node;
          position->offset = offset;

          *position_ref = position;
          position_ref = &position->next;
        }

      *position_ref = start_position;

      SVN_ERR(vtable->datasource_close(diff_baton, datasource[i]));

      *position_list[i] = position;
    }

  return SVN_NO_ERROR;
}
