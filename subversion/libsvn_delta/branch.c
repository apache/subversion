/*
 * branch.c : Element-Based Branching and Move Tracking.
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

#include "private/svn_element.h"
#include "private/svn_branch.h"
#include "private/svn_sorts_private.h"
#include "svn_private_config.h"


/* Is EID allocated (no matter whether an element with this id exists)? */
#define EID_IS_ALLOCATED(branch, eid) \
  ((eid) >= (branch)->rev_root->first_eid && (eid) < (branch)->rev_root->next_eid)

#define IS_BRANCH_ROOT_EID(branch, eid) \
  ((eid) == (branch)->root_eid)

/* Is BRANCH1 the same branch as BRANCH2? Compare by full branch-ids; don't
   require identical branch objects. */
#define BRANCH_IS_SAME_BRANCH(branch1, branch2, scratch_pool) \
  (strcmp(svn_branch_get_id(branch1, scratch_pool), \
          svn_branch_get_id(branch2, scratch_pool)) == 0)

/* Is BRANCH1 an immediate child of BRANCH2? Compare by full branch-ids; don't
   require identical branch objects. */
#define BRANCH_IS_CHILD_OF_BRANCH(branch1, branch2, scratch_pool) \
  ((branch1)->outer_branch && \
   BRANCH_IS_SAME_BRANCH((branch1)->outer_branch, branch2, scratch_pool))

svn_branch_revision_root_t *
svn_branch_revision_root_create(svn_branch_repos_t *repos,
                                svn_revnum_t rev,
                                svn_revnum_t base_rev,
                                apr_pool_t *result_pool)
{
  svn_branch_revision_root_t *rev_root
    = apr_pcalloc(result_pool, sizeof(*rev_root));

  rev_root->repos = repos;
  rev_root->rev = rev;
  rev_root->base_rev = base_rev;
  rev_root->root_branches = apr_array_make(result_pool, 0, sizeof(void *));
  rev_root->branches = svn_array_make(result_pool);
  return rev_root;
}

int
svn_branch_txn_new_eid(svn_branch_revision_root_t *rev_root)
{
  int eid = (rev_root->first_eid < 0) ? rev_root->first_eid - 1 : -2;

  rev_root->first_eid = eid;
  return eid;
}

/* Change txn-local EIDs (negative integers) in BRANCH to revision EIDs, by
 * assigning a new revision-EID (positive integer) for each one.
 */
static svn_error_t *
branch_finalize_eids(svn_branch_state_t *branch,
                     int mapping_offset,
                     apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  if (branch->root_eid < -1)
    {
      branch->root_eid = mapping_offset - branch->root_eid;
    }

  if (branch->outer_eid < -1)
    {
      branch->outer_eid = mapping_offset - branch->outer_eid;
    }

  for (hi = apr_hash_first(scratch_pool, branch->e_map);
       hi; hi = apr_hash_next(hi))
    {
      int old_eid = svn_int_hash_this_key(hi);
      svn_branch_el_rev_content_t *element = apr_hash_this_val(hi);

      if (old_eid < -1)
        {
          int new_eid = mapping_offset - old_eid;

          svn_int_hash_set(branch->e_map, old_eid, NULL);
          svn_int_hash_set(branch->e_map, new_eid, element);
        }
      if (element->parent_eid < -1)
        {
          element->parent_eid = mapping_offset - element->parent_eid;
        }
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch_txn_finalize_eids(svn_branch_revision_root_t *txn,
                             apr_pool_t *scratch_pool)
{
  int n_txn_eids = (-1) - txn->first_eid;
  int mapping_offset;
  int i;

  if (txn->first_eid == 0)
    return SVN_NO_ERROR;

  /* mapping from txn-local (negative) EID to committed (positive) EID is:
       txn_local_eid == -2  =>  committed_eid := (txn.next_eid + 0)
       txn_local_eid == -3  =>  committed_eid := (txn.next_eid + 1) ... */
  mapping_offset = txn->next_eid - 2;

  for (i = 0; i < txn->branches->nelts; i++)
    {
      svn_branch_state_t *b = APR_ARRAY_IDX(txn->branches, i, void *);

      SVN_ERR(branch_finalize_eids(b, mapping_offset, scratch_pool));
    }

  txn->next_eid += n_txn_eids;
  txn->first_eid = 0;
  return SVN_NO_ERROR;
}

svn_branch_state_t *
svn_branch_revision_root_get_root_branch(svn_branch_revision_root_t *rev_root,
                                         int top_branch_num)
{
  if (top_branch_num < 0 || top_branch_num >= rev_root->root_branches->nelts)
    return NULL;
  return APR_ARRAY_IDX(rev_root->root_branches, top_branch_num, void *);

}

const apr_array_header_t *
svn_branch_revision_root_get_branches(svn_branch_revision_root_t *rev_root,
                                      apr_pool_t *result_pool)
{
  return rev_root->branches;
}

svn_branch_state_t *
svn_branch_revision_root_get_branch_by_id(const svn_branch_revision_root_t *rev_root,
                                          const char *branch_id,
                                          apr_pool_t *scratch_pool)
{
  SVN_ITER_T(svn_branch_state_t) *bi;
  svn_branch_state_t *branch = NULL;

  for (SVN_ARRAY_ITER(bi, rev_root->branches, scratch_pool))
    {
      svn_branch_state_t *b = bi->val;

      if (strcmp(svn_branch_get_id(b, scratch_pool), branch_id) == 0)
        {
          branch = b;
          break;
        }
    }
  return branch;
}

static void
branch_validate_element(const svn_branch_state_t *branch,
                        int eid,
                        const svn_branch_el_rev_content_t *element);

/* Assert BRANCH satisfies all its invariants.
 */
static void
assert_branch_state_invariants(const svn_branch_state_t *branch,
                               apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  assert(branch->rev_root);
  if (branch->outer_branch)
    {
      assert(EID_IS_ALLOCATED(branch, branch->outer_eid));
    }
  assert(branch->outer_eid != -1);
  assert(branch->e_map);

  /* Validate elements in the map */
  for (hi = apr_hash_first(scratch_pool, branch->e_map);
       hi; hi = apr_hash_next(hi))
    {
      branch_validate_element(branch, svn_int_hash_this_key(hi),
                              apr_hash_this_val(hi));
    }
}

svn_branch_state_t *
svn_branch_state_create(int root_eid,
                        svn_branch_revision_root_t *rev_root,
                        svn_branch_state_t *outer_branch,
                        int outer_eid,
                        apr_pool_t *result_pool)
{
  svn_branch_state_t *b = apr_pcalloc(result_pool, sizeof(*b));

  b->root_eid = root_eid;
  b->rev_root = rev_root;
  b->e_map = apr_hash_make(result_pool);
  b->outer_branch = outer_branch;
  b->outer_eid = outer_eid;
  assert_branch_state_invariants(b, result_pool);
  return b;
}

svn_branch_el_rev_id_t *
svn_branch_el_rev_id_create(svn_branch_state_t *branch,
                            int eid,
                            svn_revnum_t rev,
                            apr_pool_t *result_pool)
{
  svn_branch_el_rev_id_t *id = apr_palloc(result_pool, sizeof(*id));

  id->branch = branch;
  id->eid = eid;
  id->rev = rev;
  return id;
}

svn_branch_rev_bid_eid_t *
svn_branch_rev_bid_eid_create(svn_revnum_t rev,
                              const char *branch_id,
                              int eid,
                              apr_pool_t *result_pool)
{
  svn_branch_rev_bid_eid_t *id = apr_palloc(result_pool, sizeof(*id));

  id->bid = branch_id;
  id->eid = eid;
  id->rev = rev;
  return id;
}

svn_branch_rev_bid_eid_t *
svn_branch_rev_bid_eid_dup(const svn_branch_rev_bid_eid_t *old_id,
                           apr_pool_t *result_pool)
{
  svn_branch_rev_bid_eid_t *id;

  if (! old_id)
    return NULL;

  id = apr_pmemdup(result_pool, old_id, sizeof(*id));
  id->bid = apr_pstrdup(result_pool, old_id->bid);
  return id;
}

svn_branch_el_rev_content_t *
svn_branch_el_rev_content_create(svn_branch_eid_t parent_eid,
                                 const char *name,
                                 const svn_element_payload_t *payload,
                                 apr_pool_t *result_pool)
{
  svn_branch_el_rev_content_t *content
     = apr_palloc(result_pool, sizeof(*content));

  content->parent_eid = parent_eid;
  content->name = apr_pstrdup(result_pool, name);
  content->payload = svn_element_payload_dup(payload, result_pool);
  return content;
}

svn_branch_el_rev_content_t *
svn_branch_el_rev_content_dup(const svn_branch_el_rev_content_t *old,
                              apr_pool_t *result_pool)
{
  svn_branch_el_rev_content_t *content
     = apr_pmemdup(result_pool, old, sizeof(*content));

  content->name = apr_pstrdup(result_pool, old->name);
  content->payload = svn_element_payload_dup(old->payload, result_pool);
  return content;
}

svn_boolean_t
svn_branch_el_rev_content_equal(const svn_branch_el_rev_content_t *content_left,
                                const svn_branch_el_rev_content_t *content_right,
                                apr_pool_t *scratch_pool)
{
  if (!content_left && !content_right)
    {
      return TRUE;
    }
  else if (!content_left || !content_right)
    {
      return FALSE;
    }

  if (content_left->parent_eid != content_right->parent_eid)
    {
      return FALSE;
    }
  if (strcmp(content_left->name, content_right->name) != 0)
    {
      return FALSE;
    }
  if (! svn_element_payload_equal(content_left->payload, content_right->payload,
                                  scratch_pool))
    {
      return FALSE;
    }

  return TRUE;
}


/*
 * ========================================================================
 * Branch mappings
 * ========================================================================
 */

svn_branch_subtree_t *
svn_branch_subtree_create(apr_hash_t *e_map,
                          int root_eid,
                          apr_pool_t *result_pool)
{
  svn_branch_subtree_t *subtree = apr_pcalloc(result_pool, sizeof(*subtree));

  subtree->e_map = e_map ? apr_hash_copy(result_pool, e_map)
                         : apr_hash_make(result_pool);
  subtree->root_eid = root_eid;
  subtree->subbranches = apr_hash_make(result_pool);
  return subtree;
}

svn_branch_subtree_t *
svn_branch_subtree_get_subbranch_at_eid(svn_branch_subtree_t *subtree,
                                        int eid,
                                        apr_pool_t *result_pool)
{
  subtree = svn_int_hash_get(subtree->subbranches, eid);

  return subtree;
}

/* Validate that ELEMENT is suitable for a mapping of BRANCH:EID.
 * ELEMENT->payload may be null.
 */
static void
branch_validate_element(const svn_branch_state_t *branch,
                        int eid,
                        const svn_branch_el_rev_content_t *element)
{
  SVN_ERR_ASSERT_NO_RETURN(element);

  /* Parent EID must be valid and different from this element's EID, or -1
     iff this is the branch root element. */
  SVN_ERR_ASSERT_NO_RETURN(
    IS_BRANCH_ROOT_EID(branch, eid)
    ? (element->parent_eid == -1)
    : (element->parent_eid != eid
       && EID_IS_ALLOCATED(branch, element->parent_eid)));

  /* Element name must be given, and empty iff EID is the branch root. */
  SVN_ERR_ASSERT_NO_RETURN(
    element->name
    && IS_BRANCH_ROOT_EID(branch, eid) == (*element->name == '\0'));

  /* Payload, if specified, must be in full or by reference. */
  if (element->payload)
    {
      SVN_ERR_ASSERT_NO_RETURN(svn_element_payload_invariants(element->payload));
    }
  else /* it's a subbranch root */
    {
      /* a subbranch root element must not be the branch root element */
      SVN_ERR_ASSERT_NO_RETURN(eid != branch->root_eid);
    }
}

apr_hash_t *
svn_branch_get_elements(svn_branch_state_t *branch)
{
  return branch->e_map;
}

svn_branch_el_rev_content_t *
svn_branch_get_element(const svn_branch_state_t *branch,
                       int eid)
{
  svn_branch_el_rev_content_t *element;

  SVN_ERR_ASSERT_NO_RETURN(EID_IS_ALLOCATED(branch, eid));

  element = svn_int_hash_get(branch->e_map, eid);

  if (element)
    branch_validate_element(branch, eid, element);
  return element;
}

/* In BRANCH, set element EID to ELEMENT.
 *
 * If ELEMENT is null, delete element EID. Otherwise, ELEMENT->payload may be
 * null meaning it is a subbranch-root.
 *
 * Assume ELEMENT is already allocated with sufficient lifetime.
 */
static void
branch_map_set(svn_branch_state_t *branch,
               int eid,
               svn_branch_el_rev_content_t *element)
{
  apr_pool_t *map_pool = apr_hash_pool_get(branch->e_map);

  SVN_ERR_ASSERT_NO_RETURN(EID_IS_ALLOCATED(branch, eid));
  if (element)
    branch_validate_element(branch, eid, element);

  svn_int_hash_set(branch->e_map, eid, element);
  assert_branch_state_invariants(branch, map_pool);
}

void
svn_branch_delete_element(svn_branch_state_t *branch,
                          int eid)
{
  SVN_ERR_ASSERT_NO_RETURN(EID_IS_ALLOCATED(branch, eid));

  branch_map_set(branch, eid, NULL);
}

void
svn_branch_update_element(svn_branch_state_t *branch,
                          int eid,
                          svn_branch_eid_t new_parent_eid,
                          const char *new_name,
                          const svn_element_payload_t *new_payload)
{
  apr_pool_t *map_pool = apr_hash_pool_get(branch->e_map);
  svn_branch_el_rev_content_t *element
    = svn_branch_el_rev_content_create(new_parent_eid, new_name, new_payload,
                                       map_pool);

  /* EID must be a valid element id */
  SVN_ERR_ASSERT_NO_RETURN(EID_IS_ALLOCATED(branch, eid));
  /* NEW_PAYLOAD must be specified, either in full or by reference */
  SVN_ERR_ASSERT_NO_RETURN(new_payload);

  /* Insert the new version */
  branch_map_set(branch, eid, element);
}

void
svn_branch_update_subbranch_root_element(svn_branch_state_t *branch,
                                         int eid,
                                         svn_branch_eid_t new_parent_eid,
                                         const char *new_name)
{
  apr_pool_t *map_pool = apr_hash_pool_get(branch->e_map);
  svn_branch_el_rev_content_t *element
    = svn_branch_el_rev_content_create(new_parent_eid, new_name,
                                       NULL /*payload*/, map_pool);

  /* EID must be a valid element id */
  SVN_ERR_ASSERT_NO_RETURN(EID_IS_ALLOCATED(branch, eid));

  /* Insert the new version */
  branch_map_set(branch, eid, element);
}

static void
map_purge_orphans(apr_hash_t *e_map,
                  int root_eid,
                  apr_pool_t *scratch_pool);

svn_branch_subtree_t *
svn_branch_get_subtree(const svn_branch_state_t *branch,
                       int eid,
                       apr_pool_t *result_pool)
{
  svn_branch_subtree_t *new_subtree;
  svn_branch_el_rev_content_t *subtree_root_element;
  SVN_ITER_T(svn_branch_state_t) *bi;

  SVN_BRANCH_SEQUENCE_POINT(branch);

  new_subtree = svn_branch_subtree_create(branch->e_map, eid,
                                          result_pool);

  /* Purge orphans */
  map_purge_orphans(new_subtree->e_map, new_subtree->root_eid, result_pool);

  /* Remove 'parent' and 'name' attributes from subtree root element */
  subtree_root_element
    = svn_int_hash_get(new_subtree->e_map, new_subtree->root_eid);
  svn_int_hash_set(new_subtree->e_map, new_subtree->root_eid,
                   svn_branch_el_rev_content_create(
                     -1, "", subtree_root_element->payload, result_pool));

  /* Add subbranches */
  for (SVN_ARRAY_ITER(bi, svn_branch_get_immediate_subbranches(
                            branch, result_pool, result_pool), result_pool))
    {
      svn_branch_state_t *subbranch = bi->val;
      const char *subbranch_relpath_in_subtree
        = svn_branch_subtree_get_path_by_eid(new_subtree, subbranch->outer_eid,
                                             bi->iterpool);

      /* Is it pathwise at or below EID? If so, add it into the subtree. */
      if (subbranch_relpath_in_subtree)
        {
          svn_branch_subtree_t *this_subtree
            = svn_branch_get_subtree(subbranch, subbranch->root_eid, result_pool);

          svn_int_hash_set(new_subtree->subbranches, subbranch->outer_eid,
                           this_subtree);
        }
    }
  return new_subtree;
}

/* Purge entries from E_MAP that don't connect, via parent directory hierarchy,
 * to ROOT_EID. In other words, remove elements that have been implicitly
 * deleted.
 *
 * ROOT_EID must be present in E_MAP.
 *
 * ### Does not detect cycles: current implementation will not purge a cycle
 *     that is disconnected from ROOT_EID. This could be a problem.
 */
static void
map_purge_orphans(apr_hash_t *e_map,
                  int root_eid,
                  apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  svn_boolean_t changed;

  SVN_ERR_ASSERT_NO_RETURN(svn_int_hash_get(e_map, root_eid));

  do
    {
      changed = FALSE;

      for (hi = apr_hash_first(scratch_pool, e_map);
           hi; hi = apr_hash_next(hi))
        {
          int this_eid = svn_int_hash_this_key(hi);
          svn_branch_el_rev_content_t *this_element = apr_hash_this_val(hi);

          if (this_eid != root_eid)
            {
              svn_branch_el_rev_content_t *parent_element
                = svn_int_hash_get(e_map, this_element->parent_eid);

              /* Purge if parent is deleted */
              if (! parent_element)
                {
                  SVN_DBG(("purge orphan: e%d", this_eid));
                  svn_int_hash_set(e_map, this_eid, NULL);
                  changed = TRUE;
                }
              else
                SVN_ERR_ASSERT_NO_RETURN(parent_element->payload);
            }
        }
    }
  while (changed);
}

void
svn_branch_purge_r(svn_branch_state_t *branch,
                   apr_pool_t *scratch_pool)
{
  SVN_ITER_T(svn_branch_state_t) *bi;

  /* first, remove elements that have no parent element */
  map_purge_orphans(branch->e_map, branch->root_eid, scratch_pool);

  /* second, remove subbranches that have no subbranch-root element */
  for (SVN_ARRAY_ITER(bi, svn_branch_get_immediate_subbranches(
                            branch, scratch_pool, scratch_pool), scratch_pool))
    {
      svn_branch_state_t *b = bi->val;

      if (svn_branch_get_element(branch, b->outer_eid))
        {
          svn_branch_purge_r(b, bi->iterpool);
        }
      else
        {
          svn_branch_delete_branch_r(b, bi->iterpool);
        }
    }
}

const char *
svn_branch_get_root_rrpath(const svn_branch_state_t *branch,
                           apr_pool_t *result_pool)
{
  const char *root_rrpath;

  if (branch->outer_branch)
    {
      root_rrpath
        = svn_branch_get_rrpath_by_eid(branch->outer_branch, branch->outer_eid,
                                       result_pool);
    }
  else
    {
      root_rrpath = "";
    }

  SVN_ERR_ASSERT_NO_RETURN(root_rrpath);
  return root_rrpath;
}

const char *
svn_branch_subtree_get_path_by_eid(const svn_branch_subtree_t *subtree,
                                   int eid,
                                   apr_pool_t *result_pool)
{
  const char *path = "";
  svn_branch_el_rev_content_t *element;

  for (; eid != subtree->root_eid; eid = element->parent_eid)
    {
      element = svn_int_hash_get(subtree->e_map, eid);
      if (! element)
        return NULL;
      path = svn_relpath_join(element->name, path, result_pool);
    }
  SVN_ERR_ASSERT_NO_RETURN(eid == subtree->root_eid);
  return path;
}

const char *
svn_branch_get_path_by_eid(const svn_branch_state_t *branch,
                           int eid,
                           apr_pool_t *result_pool)
{
  const char *path = "";
  svn_branch_el_rev_content_t *element;

  SVN_ERR_ASSERT_NO_RETURN(EID_IS_ALLOCATED(branch, eid));

  for (; ! IS_BRANCH_ROOT_EID(branch, eid); eid = element->parent_eid)
    {
      element = svn_branch_get_element(branch, eid);
      if (! element)
        return NULL;
      path = svn_relpath_join(element->name, path, result_pool);
    }
  SVN_ERR_ASSERT_NO_RETURN(IS_BRANCH_ROOT_EID(branch, eid));
  return path;
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

int
svn_branch_get_eid_by_path(const svn_branch_state_t *branch,
                           const char *path,
                           apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  /* ### This is a crude, linear search */
  for (hi = apr_hash_first(scratch_pool, branch->e_map);
       hi; hi = apr_hash_next(hi))
    {
      int eid = svn_int_hash_this_key(hi);
      const char *this_path = svn_branch_get_path_by_eid(branch, eid,
                                                         scratch_pool);

      if (! this_path)
        {
          /* Mapping is not complete; this element is in effect not present. */
          continue;
        }
      if (strcmp(path, this_path) == 0)
        {
          return eid;
        }
    }

  return -1;
}

int
svn_branch_get_eid_by_rrpath(svn_branch_state_t *branch,
                             const char *rrpath,
                             apr_pool_t *scratch_pool)
{
  const char *path = svn_relpath_skip_ancestor(svn_branch_get_root_rrpath(
                                                 branch, scratch_pool),
                                               rrpath);
  int eid = -1;

  if (path)
    {
      eid = svn_branch_get_eid_by_path(branch, path, scratch_pool);
    }
  return eid;
}

svn_error_t *
svn_branch_map_add_subtree(svn_branch_state_t *to_branch,
                           int to_eid,
                           svn_branch_eid_t new_parent_eid,
                           const char *new_name,
                           svn_branch_subtree_t new_subtree,
                           apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  svn_branch_el_rev_content_t *new_root_content;

  if (new_subtree.subbranches && apr_hash_count(new_subtree.subbranches))
    {
      return svn_error_createf(SVN_ERR_BRANCHING, NULL,
                               _("Adding or copying a subtree containing "
                                 "subbranches is not implemented"));
    }

  /* Get a new EID for the root element, if not given. */
  if (to_eid == -1)
    {
      to_eid = svn_branch_txn_new_eid(to_branch->rev_root);
    }

  /* Create the new subtree root element */
  new_root_content = svn_int_hash_get(new_subtree.e_map, new_subtree.root_eid);
  if (new_root_content->payload)
    svn_branch_update_element(to_branch, to_eid,
                              new_parent_eid, new_name,
                              new_root_content->payload);
  else
    svn_branch_update_subbranch_root_element(to_branch, to_eid,
                                             new_parent_eid, new_name);

  /* Process its immediate children */
  for (hi = apr_hash_first(scratch_pool, new_subtree.e_map);
       hi; hi = apr_hash_next(hi))
    {
      int this_from_eid = svn_int_hash_this_key(hi);
      svn_branch_el_rev_content_t *from_element = apr_hash_this_val(hi);

      if (from_element->parent_eid == new_subtree.root_eid)
        {
          svn_branch_subtree_t this_subtree;

          /* Recurse. (We don't try to check whether it's a directory node,
             as we might not have the node kind in the map.) */
          this_subtree.e_map = new_subtree.e_map;
          this_subtree.root_eid = this_from_eid;
          this_subtree.subbranches = apr_hash_make(scratch_pool);
          SVN_ERR(svn_branch_map_add_subtree(to_branch, -1 /*to_eid*/,
                                             to_eid, from_element->name,
                                             this_subtree, scratch_pool));
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch_instantiate_subtree(svn_branch_state_t *to_branch,
                               svn_branch_eid_t new_parent_eid,
                               const char *new_name,
                               svn_branch_subtree_t new_subtree,
                               apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  svn_branch_el_rev_content_t *new_root_content;

  /* Source element must not be the same as the target parent element */
  if (new_subtree.root_eid == new_parent_eid)
    {
      return svn_error_createf(SVN_ERR_BRANCHING, NULL,
                               _("Cannot branch from e%d to %s e%d/%s: "
                                 "target element cannot be its own parent"),
                               new_subtree.root_eid,
                               svn_branch_get_id(to_branch, scratch_pool),
                               new_parent_eid, new_name);
    }

  /* Instantiate the root element of NEW_SUBTREE */
  new_root_content = svn_int_hash_get(new_subtree.e_map, new_subtree.root_eid);
  if (new_root_content->payload)
    svn_branch_update_element(to_branch, new_subtree.root_eid,
                              new_parent_eid, new_name,
                              new_root_content->payload);
  else
    svn_branch_update_subbranch_root_element(to_branch, new_subtree.root_eid,
                                             new_parent_eid, new_name);

  /* Instantiate all the children of NEW_SUBTREE */
  for (hi = apr_hash_first(scratch_pool, new_subtree.e_map);
       hi; hi = apr_hash_next(hi))
    {
      int this_eid = svn_int_hash_this_key(hi);
      svn_branch_el_rev_content_t *this_element = apr_hash_this_val(hi);

      if (this_eid != new_subtree.root_eid)
        {
          branch_map_set(to_branch, this_eid, this_element);
        }
    }

  /* branch any subbranches */
  {
    SVN_ITER_T(svn_branch_subtree_t) *bi;

    for (SVN_HASH_ITER(bi, scratch_pool, new_subtree.subbranches))
      {
        int this_outer_eid = svn_int_hash_this_key(bi->apr_hi);
        svn_branch_subtree_t *this_subtree = bi->val;
        svn_branch_state_t *new_branch;

        /* branch this subbranch into NEW_BRANCH (recursing) */
        new_branch = svn_branch_add_new_branch(to_branch->rev_root,
                                               to_branch, this_outer_eid,
                                               this_subtree->root_eid,
                                               bi->iterpool);

        SVN_ERR(svn_branch_instantiate_subtree(new_branch, -1, "", *this_subtree,
                                               bi->iterpool));
      }
  }

  return SVN_NO_ERROR;
}

apr_array_header_t *
svn_branch_get_immediate_subbranches(const svn_branch_state_t *branch,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool)
{
  svn_array_t *subbranches = svn_array_make(result_pool);
  SVN_ITER_T(svn_branch_state_t) *bi;

  for (SVN_ARRAY_ITER(bi, branch->rev_root->branches, scratch_pool))
    {
      /* Is it an immediate child? */
      if (bi->val->outer_branch == branch)
        SVN_ARRAY_PUSH(subbranches) = bi->val;
    }
  return subbranches;
}

svn_branch_state_t *
svn_branch_get_subbranch_at_eid(svn_branch_state_t *branch,
                                int eid,
                                apr_pool_t *scratch_pool)
{
  SVN_ITER_T(svn_branch_state_t) *bi;

  /* TODO: more efficient to search in branch->rev_root->branches */
  for (SVN_ARRAY_ITER(bi, svn_branch_get_immediate_subbranches(
                            branch, scratch_pool, scratch_pool), scratch_pool))
    {
      if (bi->val->outer_eid == eid)
        return bi->val;
    }
  return NULL;
}

svn_branch_state_t *
svn_branch_add_new_branch(svn_branch_revision_root_t *rev_root,
                          svn_branch_state_t *outer_branch,
                          int outer_eid,
                          int root_eid,
                          apr_pool_t *scratch_pool)
{
  svn_branch_state_t *new_branch;

  SVN_ERR_ASSERT_NO_RETURN(!outer_branch || outer_branch->rev_root == rev_root);
  SVN_ERR_ASSERT_NO_RETURN(root_eid != -1);

  if (! outer_branch)
    outer_eid = rev_root->root_branches->nelts;

  new_branch = svn_branch_state_create(root_eid, rev_root,
                                       outer_branch, outer_eid,
                                       rev_root->branches->pool);

  /* A branch must not already exist at this outer element */
  SVN_ERR_ASSERT_NO_RETURN(!outer_branch ||
                           svn_branch_get_subbranch_at_eid(
                             outer_branch, outer_eid, scratch_pool) == NULL);

  SVN_ARRAY_PUSH(rev_root->branches) = new_branch;
  if (!outer_branch)
    SVN_ARRAY_PUSH(rev_root->root_branches) = new_branch;

  return new_branch;
}

/* Remove branch BRANCH from the list of branches in REV_ROOT.
 */
static void
svn_branch_revision_root_delete_branch(
                                svn_branch_revision_root_t *rev_root,
                                svn_branch_state_t *branch,
                                apr_pool_t *scratch_pool)
{
  SVN_ITER_T(svn_branch_state_t) *bi;

  SVN_ERR_ASSERT_NO_RETURN(branch->rev_root == rev_root);

  for (SVN_ARRAY_ITER(bi, rev_root->branches, scratch_pool))
    {
      if (bi->val == branch)
        {
          SVN_DBG(("deleting branch b%s e%d",
                   svn_branch_get_id(bi->val, bi->iterpool),
                   bi->val->root_eid));
          svn_sort__array_delete(rev_root->branches, bi->i, 1);
          break;
        }
    }
  for (SVN_ARRAY_ITER(bi, rev_root->root_branches, scratch_pool))
    {
      if (bi->val == branch)
        {
          SVN_DBG(("deleting root-branch b%s e%d",
                   svn_branch_get_id(bi->val, bi->iterpool),
                   bi->val->root_eid));
          svn_sort__array_delete(rev_root->root_branches, bi->i, 1);
          break;
        }
    }
}

void
svn_branch_delete_branch_r(svn_branch_state_t *branch,
                           apr_pool_t *scratch_pool)
{
  SVN_ITER_T(svn_branch_state_t) *bi;

  for (SVN_ARRAY_ITER(bi, svn_branch_get_immediate_subbranches(
                            branch, scratch_pool, scratch_pool), scratch_pool))
    {
      svn_branch_delete_branch_r(bi->val, bi->iterpool);
    }

  svn_branch_revision_root_delete_branch(branch->rev_root,
                                         branch, scratch_pool);
}


/*
 * ========================================================================
 * Parsing and Serializing
 * ========================================================================
 */

svn_string_t *
svn_branch_get_default_r0_metadata(apr_pool_t *result_pool)
{
  static const char *default_repos_info
    = "r0: eids 0 1 branches 1\n"
      "B0 root-eid 0 num-eids 1  # at /\n"
      "e0: normal -1 .\n";

  return svn_string_create(default_repos_info, result_pool);
}

/*  */
static svn_error_t *
parse_branch_line(char *bid_p,
                  int *root_eid_p,
                  int *num_eids_p,
                  svn_stream_t *stream,
                  apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *line;
  svn_boolean_t eof;
  int n;

  /* Read a line */
  SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, scratch_pool));
  SVN_ERR_ASSERT(!eof);

  n = sscanf(line->data, "%s root-eid %d num-eids %d",
             bid_p, root_eid_p, num_eids_p);
  SVN_ERR_ASSERT(n >= 3);  /* C std is unclear on whether '%n' counts */

  return SVN_NO_ERROR;
}

/*  */
static svn_error_t *
parse_element_line(int *eid_p,
                   svn_boolean_t *is_subbranch_p,
                   int *parent_eid_p,
                   const char **name_p,
                   svn_stream_t *stream,
                   apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *line;
  svn_boolean_t eof;
  char kind[10];
  int n;
  int offset;

  /* Read a line */
  SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, scratch_pool));
  SVN_ERR_ASSERT(!eof);

  n = sscanf(line->data, "e%d: %9s %d%n",
             eid_p,
             kind, parent_eid_p, &offset);
  SVN_ERR_ASSERT(n >= 3);  /* C std is unclear on whether '%n' counts */
  SVN_ERR_ASSERT(line->data[offset] == ' ');
  *name_p = line->data + offset + 1;

  *is_subbranch_p = (strcmp(kind, "subbranch") == 0);

  if (strcmp(*name_p, "(null)") == 0)
    *name_p = NULL;
  else if (strcmp(*name_p, ".") == 0)
    *name_p = "";

  return SVN_NO_ERROR;
}

const char *
svn_branch_id_nest(const char *outer_bid,
                   int outer_eid,
                   apr_pool_t *result_pool)
{
  if (!outer_bid)
    return apr_psprintf(result_pool, "B%d", outer_eid);

  return apr_psprintf(result_pool, "%s.%d", outer_bid, outer_eid);
}

void
svn_branch_id_unnest(const char **outer_bid,
                     int *outer_eid,
                     const char *bid,
                     apr_pool_t *result_pool)
{
  char *last_dot = strrchr(bid, '.');

  if (last_dot) /* BID looks like "B3.11" or "B3.11.22" etc. */
    {
      *outer_bid = apr_pstrndup(result_pool, bid, last_dot - bid);
      *outer_eid = atoi(last_dot + 1);
    }
  else /* looks like "B0" or B22" (with no dot) */
    {
      *outer_bid = NULL;
      *outer_eid = atoi(bid + 1);
    }
}

/* Create a new branch *NEW_BRANCH, initialized
 * with info parsed from STREAM, allocated in RESULT_POOL.
 */
static svn_error_t *
svn_branch_state_parse(svn_branch_state_t **new_branch,
                       svn_branch_revision_root_t *rev_root,
                       svn_stream_t *stream,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  char bid[1000];
  int root_eid, num_eids;
  svn_branch_state_t *branch_state;
  svn_branch_state_t *outer_branch;
  int outer_eid;
  int i;

  SVN_ERR(parse_branch_line(bid, &root_eid, &num_eids,
                            stream, scratch_pool));

  /* Find the outer branch and outer EID */
  {
    const char *outer_bid;

    svn_branch_id_unnest(&outer_bid, &outer_eid, bid, scratch_pool);
    if (outer_bid)
      {
        outer_branch
          = svn_branch_revision_root_get_branch_by_id(rev_root, outer_bid,
                                                      scratch_pool);
      }
    else
      outer_branch = NULL;
  }
  branch_state = svn_branch_state_create(root_eid, rev_root,
                                         outer_branch, outer_eid,
                                         result_pool);

  /* Read in the structure. Set the payload of each normal element to a
     (branch-relative) reference. */
  for (i = 0; i < num_eids; i++)
    {
      int eid, this_parent_eid;
      const char *this_name;
      svn_boolean_t is_subbranch;

      SVN_ERR(parse_element_line(&eid,
                                 &is_subbranch, &this_parent_eid, &this_name,
                                 stream, scratch_pool));

      if (this_name)
        {
          if (! is_subbranch)
            {
              svn_element_payload_t *payload
                = svn_element_payload_create_ref(rev_root->rev, bid, eid,
                                                 result_pool);
              svn_branch_update_element(
                branch_state, eid, this_parent_eid, this_name, payload);
            }
          else
            {
              svn_branch_update_subbranch_root_element(
                branch_state, eid, this_parent_eid, this_name);
            }
        }
    }

  *new_branch = branch_state;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch_revision_root_parse(svn_branch_revision_root_t **rev_root_p,
                               svn_branch_repos_t *repos,
                               svn_stream_t *stream,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  svn_branch_revision_root_t *rev_root;
  svn_revnum_t rev;
  int first_eid, next_eid;
  int num_branches;
  svn_stringbuf_t *line;
  svn_boolean_t eof;
  int n;
  int j;

  SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, scratch_pool));
  SVN_ERR_ASSERT(! eof);
  n = sscanf(line->data, "r%ld: eids %d %d "
                         "branches %d",
             &rev,
             &first_eid, &next_eid,
             &num_branches);
  SVN_ERR_ASSERT(n == 4);

  rev_root = svn_branch_revision_root_create(repos, rev, rev - 1,
                                             result_pool);
  rev_root->first_eid = first_eid;
  rev_root->next_eid = next_eid;

  /* parse the branches */
  for (j = 0; j < num_branches; j++)
    {
      svn_branch_state_t *branch;

      SVN_ERR(svn_branch_state_parse(&branch, rev_root, stream,
                                     result_pool, scratch_pool));
      SVN_ARRAY_PUSH(rev_root->branches) = branch;

      /* Note the root branches */
      if (! branch->outer_branch)
        {
          APR_ARRAY_PUSH(rev_root->root_branches, void *) = branch;
        }
    }

  *rev_root_p = rev_root;
  return SVN_NO_ERROR;
}

/* ### Duplicated in svnmover.c. */
static int
sort_compare_items_by_eid(const svn_sort__item_t *a,
                          const svn_sort__item_t *b)
{
  int eid_a = *(const int *)a->key;
  int eid_b = *(const int *)b->key;

  return eid_a - eid_b;
}

/* Write to STREAM a parseable representation of BRANCH.
 */
svn_error_t *
svn_branch_state_serialize(svn_stream_t *stream,
                           svn_branch_state_t *branch,
                           apr_pool_t *scratch_pool)
{
  const char *branch_root_rrpath = svn_branch_get_root_rrpath(branch,
                                                              scratch_pool);
  SVN_ITER_T(svn_branch_el_rev_content_t) *hi;

  SVN_ERR(svn_stream_printf(stream, scratch_pool,
                            "%s root-eid %d num-eids %d  # at /%s\n",
                            svn_branch_get_id(branch, scratch_pool),
                            branch->root_eid,
                            apr_hash_count(branch->e_map),
                            branch_root_rrpath));

  map_purge_orphans(branch->e_map, branch->root_eid, scratch_pool);

  for (SVN_HASH_ITER_SORTED(hi, branch->e_map, sort_compare_items_by_eid,
                            scratch_pool))
    {
      int eid = *(const int *)hi->key;
      svn_branch_el_rev_content_t *element = svn_branch_get_element(branch, eid);
      int parent_eid;
      const char *name;

      SVN_ERR_ASSERT(element);
      parent_eid = element->parent_eid;
      name = element->name[0] ? element->name : ".";
      SVN_ERR(svn_stream_printf(stream, scratch_pool,
                                "e%d: %s %d %s\n",
                                eid,
                                element ? (element->payload ? "normal" : "subbranch")
                                     : "none",
                                parent_eid, name));
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch_revision_root_serialize(svn_stream_t *stream,
                                   svn_branch_revision_root_t *rev_root,
                                   apr_pool_t *scratch_pool)
{
  SVN_ITER_T(svn_branch_state_t) *bi;

  SVN_ERR(svn_stream_printf(stream, scratch_pool,
                            "r%ld: eids %d %d "
                            "branches %d\n",
                            rev_root->rev,
                            rev_root->first_eid, rev_root->next_eid,
                            rev_root->branches->nelts));

  for (SVN_ARRAY_ITER(bi, rev_root->branches, scratch_pool))
    SVN_ERR(svn_branch_state_serialize(stream, bi->val, bi->iterpool));
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
          const char *relpath_to_subbranch;
          const char *relpath_in_subbranch;

          relpath_to_subbranch
            = svn_branch_get_path_by_eid(root_branch, subbranch->outer_eid,
                                         scratch_pool);

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


/*
 * ========================================================================
 */

const char *
svn_branch_get_id(svn_branch_state_t *branch,
                  apr_pool_t *result_pool)
{
  const char *id = "";

  while (branch->outer_branch)
    {
      id = apr_psprintf(result_pool, ".%d%s", branch->outer_eid, id);
      branch = branch->outer_branch;
    }
  id = apr_psprintf(result_pool, "B%d%s", branch->outer_eid, id);
  return id;
}

