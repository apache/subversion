/*
 * branch_nested.c : Nested Branches
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

#include <assert.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_iter.h"

#include "private/svn_branch_nested.h"
#include "private/svn_branch_repos.h"
#include "svn_private_config.h"


void
svn_branch_get_outer_branch_and_eid(svn_branch_state_t **outer_branch_p,
                                    int *outer_eid_p,
                                    const svn_branch_state_t *branch,
                                    apr_pool_t *scratch_pool)
{
  const char *outer_bid;

  svn_branch_id_unnest(&outer_bid, outer_eid_p, branch->bid, scratch_pool);
  *outer_branch_p = NULL;
  if (outer_bid)
    {
      *outer_branch_p
        = svn_branch_txn_get_branch_by_id(branch->txn, outer_bid,
                                          scratch_pool);
    }
}

const char *
svn_branch_get_root_rrpath(const svn_branch_state_t *branch,
                           apr_pool_t *result_pool)
{
  svn_branch_state_t *outer_branch;
  int outer_eid;
  const char *root_rrpath;

  svn_branch_get_outer_branch_and_eid(&outer_branch, &outer_eid, branch,
                                      result_pool);
  if (outer_branch)
    {
      root_rrpath
        = svn_branch_get_rrpath_by_eid(outer_branch, outer_eid, result_pool);
    }
  else
    {
      root_rrpath = "";
    }

  SVN_ERR_ASSERT_NO_RETURN(root_rrpath);
  return root_rrpath;
}

const char *
svn_branch_get_rrpath_by_eid(const svn_branch_state_t *branch,
                             int eid,
                             apr_pool_t *result_pool)
{
  const char *path = svn_branch_get_path_by_eid(branch, eid, result_pool);
  const char *rrpath = NULL;

  if (path)
    {
      rrpath = svn_relpath_join(svn_branch_get_root_rrpath(branch, result_pool),
                                path, result_pool);
    }
  return rrpath;
}

svn_branch_state_t *
svn_branch_get_subbranch_at_eid(svn_branch_state_t *branch,
                                int eid,
                                apr_pool_t *scratch_pool)
{
  svn_element_content_t *element = svn_branch_get_element(branch, eid);

  if (element && element->payload->is_subbranch_root)
    {
      const char *branch_id = svn_branch_get_id(branch, scratch_pool);
      const char *subbranch_id = svn_branch_id_nest(branch_id, eid,
                                                    scratch_pool);

      return svn_branch_txn_get_branch_by_id(branch->txn, subbranch_id,
                                             scratch_pool);
    }
  return NULL;
}

apr_array_header_t *
svn_branch_get_immediate_subbranches(svn_branch_state_t *branch,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool)
{
  svn_array_t *subbranches = svn_array_make(result_pool);
  const char *branch_id = svn_branch_get_id(branch, scratch_pool);
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, svn_branch_get_elements(branch));
       hi; hi = apr_hash_next(hi))
    {
      int eid = svn_int_hash_this_key(hi);
      svn_element_content_t *element = apr_hash_this_val(hi);

      if (element->payload->is_subbranch_root)
        {
          const char *subbranch_id
            = svn_branch_id_nest(branch_id, eid, scratch_pool);
          svn_branch_state_t *subbranch
            = svn_branch_txn_get_branch_by_id(branch->txn, subbranch_id,
                                              scratch_pool);

          SVN_ARRAY_PUSH(subbranches) = subbranch;
        }
    }
  return subbranches;
}

svn_branch_subtree_t *
svn_branch_subtree_create(apr_hash_t *e_map,
                          int root_eid,
                          apr_pool_t *result_pool)
{
  svn_branch_subtree_t *subtree = apr_pcalloc(result_pool, sizeof(*subtree));

  subtree->tree = svn_element_tree_create(e_map, root_eid, result_pool);
  subtree->subbranches = apr_hash_make(result_pool);
  return subtree;
}

svn_branch_subtree_t *
svn_branch_get_subtree(svn_branch_state_t *branch,
                       int eid,
                       apr_pool_t *result_pool)
{
  svn_branch_subtree_t *new_subtree;
  SVN_ITER_T(svn_branch_state_t) *bi;

  SVN_BRANCH_SEQUENCE_POINT(branch);

  new_subtree
    = svn_branch_subtree_create(
        svn_branch_get_element_tree_at_eid(branch, eid, result_pool)->e_map,
        eid, result_pool);
  new_subtree->predecessor = svn_branch_rev_bid_dup(branch->predecessor,
                                                    result_pool);

  /* Add subbranches */
  for (SVN_ARRAY_ITER(bi, svn_branch_get_immediate_subbranches(
                            branch, result_pool, result_pool), result_pool))
    {
      svn_branch_state_t *subbranch = bi->val;
      const char *outer_bid;
      int outer_eid;
      const char *subbranch_relpath_in_subtree;

      svn_branch_id_unnest(&outer_bid, &outer_eid, subbranch->bid,
                           bi->iterpool);
      subbranch_relpath_in_subtree
        = svn_element_tree_get_path_by_eid(new_subtree->tree, outer_eid,
                                           bi->iterpool);

      /* Is it pathwise at or below EID? If so, add it into the subtree. */
      if (subbranch_relpath_in_subtree)
        {
          svn_branch_subtree_t *this_subtree
            = svn_branch_get_subtree(subbranch, svn_branch_root_eid(subbranch),
                                     result_pool);

          svn_int_hash_set(new_subtree->subbranches, outer_eid,
                           this_subtree);
        }
    }
  return new_subtree;
}

svn_branch_subtree_t *
svn_branch_subtree_get_subbranch_at_eid(svn_branch_subtree_t *subtree,
                                        int eid,
                                        apr_pool_t *result_pool)
{
  subtree = svn_int_hash_get(subtree->subbranches, eid);

  return subtree;
}

svn_error_t *
svn_branch_instantiate_elements_r(svn_branch_state_t *to_branch,
                                  svn_branch_subtree_t elements,
                                  apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_branch_instantiate_elements(to_branch, elements.tree,
                                          scratch_pool));

  /* branch any subbranches */
  {
    SVN_ITER_T(svn_branch_subtree_t) *bi;

    for (SVN_HASH_ITER(bi, scratch_pool, elements.subbranches))
      {
        int this_outer_eid = svn_int_hash_this_key(bi->apr_hi);
        svn_branch_subtree_t *this_subtree = bi->val;
        const char *new_branch_id;
        svn_branch_state_t *new_branch;

        /* branch this subbranch into NEW_BRANCH (recursing) */
        new_branch_id = svn_branch_id_nest(to_branch->bid, this_outer_eid,
                                           bi->iterpool);
        new_branch = svn_branch_txn_add_new_branch(to_branch->txn,
                                                   new_branch_id,
                                                   this_subtree->predecessor,
                                                   this_subtree->tree->root_eid,
                                                   bi->iterpool);

        SVN_ERR(svn_branch_instantiate_elements_r(new_branch, *this_subtree,
                                                  bi->iterpool));
      }
  }

  return SVN_NO_ERROR;
}

/*
 * ========================================================================
 */

void
svn_branch_find_nested_branch_element_by_relpath(
                                svn_branch_state_t **branch_p,
                                int *eid_p,
                                svn_branch_state_t *root_branch,
                                const char *relpath,
                                apr_pool_t *scratch_pool)
{
  /* The path we're looking for is (path-wise) in this branch. See if it
     is also in a sub-branch. */
  while (TRUE)
    {
      SVN_ITER_T(svn_branch_state_t) *bi;
      svn_boolean_t found = FALSE;

      for (SVN_ARRAY_ITER(bi, svn_branch_get_immediate_subbranches(
                                root_branch, scratch_pool, scratch_pool),
                          scratch_pool))
        {
          svn_branch_state_t *subbranch = bi->val;
          svn_branch_state_t *outer_branch;
          int outer_eid;
          const char *relpath_to_subbranch;
          const char *relpath_in_subbranch;

          svn_branch_get_outer_branch_and_eid(&outer_branch, &outer_eid,
                                              subbranch, scratch_pool);

          relpath_to_subbranch
            = svn_branch_get_path_by_eid(root_branch, outer_eid, scratch_pool);

          relpath_in_subbranch
            = svn_relpath_skip_ancestor(relpath_to_subbranch, relpath);
          if (relpath_in_subbranch)
            {
              root_branch = subbranch;
              relpath = relpath_in_subbranch;
              found = TRUE;
              break;
            }
        }
      if (! found)
        {
          break;
        }
    }

  *branch_p = root_branch;
  if (eid_p)
    *eid_p = svn_branch_get_eid_by_path(root_branch, relpath, scratch_pool);
}

svn_error_t *
svn_branch_repos_find_el_rev_by_path_rev(svn_branch_el_rev_id_t **el_rev_p,
                                const svn_branch_repos_t *repos,
                                svn_revnum_t revnum,
                                const char *branch_id,
                                const char *relpath,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  svn_branch_el_rev_id_t *el_rev = apr_palloc(result_pool, sizeof(*el_rev));
  svn_branch_state_t *branch;

  SVN_ERR(svn_branch_repos_get_branch_by_id(&branch,
                                            repos, revnum, branch_id,
                                            scratch_pool));
  el_rev->rev = revnum;
  svn_branch_find_nested_branch_element_by_relpath(&el_rev->branch,
                                                   &el_rev->eid,
                                                   branch, relpath,
                                                   scratch_pool);

  /* Any relpath must at least be within the originally given branch */
  SVN_ERR_ASSERT_NO_RETURN(el_rev->branch);
  *el_rev_p = el_rev;
  return SVN_NO_ERROR;
}

