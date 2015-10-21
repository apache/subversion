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
#include "svn_pools.h"

#include "private/svn_element.h"
#include "private/svn_branch.h"
#include "private/svn_sorts_private.h"

#include "branch_private.h"

#include "svn_private_config.h"


/* Is EID allocated (no matter whether an element with this id exists)? */
#define EID_IS_ALLOCATED(branch, eid) \
  ((eid) >= (branch)->txn->first_eid && (eid) < (branch)->txn->next_eid)

#define IS_BRANCH_ROOT_EID(branch, eid) \
  ((eid) == (branch)->priv->element_tree->root_eid)

/* Is BRANCH1 the same branch as BRANCH2? Compare by full branch-ids; don't
   require identical branch objects. */
#define BRANCH_IS_SAME_BRANCH(branch1, branch2, scratch_pool) \
  (strcmp(svn_branch_get_id(branch1, scratch_pool), \
          svn_branch_get_id(branch2, scratch_pool)) == 0)

struct svn_branch_txn_priv_t
{
  /* All branches. */
  apr_array_header_t *branches;

};

struct svn_branch_state_priv_t
{
  /* EID -> svn_branch_el_rev_content_t mapping. */
  svn_element_tree_t *element_tree;

};

static svn_branch_state_t *
branch_state_create(const char *bid,
                    svn_branch_rev_bid_t *predecessor,
                    int root_eid,
                    svn_branch_txn_t *txn,
                    apr_pool_t *result_pool);

/*  */
static apr_pool_t *
branch_state_pool_get(svn_branch_state_t *branch)
{
  return apr_hash_pool_get(branch->priv->element_tree->e_map);
}

/* ### Layering: we didn't want to look at the whole repos in here, but
   copying seems to require it. */
svn_error_t *
svn_branch_repos_get_branch_by_id(svn_branch_state_t **branch_p,
                                  const svn_branch_repos_t *repos,
                                  svn_revnum_t revnum,
                                  const char *branch_id,
                                  apr_pool_t *scratch_pool);

/*  */
static svn_error_t *
branch_in_rev_or_txn(svn_branch_state_t **src_branch,
                     const svn_branch_rev_bid_eid_t *src_el_rev,
                     svn_branch_txn_t *txn,
                     apr_pool_t *result_pool)
{
  if (SVN_IS_VALID_REVNUM(src_el_rev->rev))
    {
      SVN_ERR(svn_branch_repos_get_branch_by_id(src_branch,
                                                txn->repos,
                                                src_el_rev->rev,
                                                src_el_rev->bid,
                                                result_pool));
    }
  else
    {
      *src_branch
        = svn_branch_txn_get_branch_by_id(txn, src_el_rev->bid, result_pool);
    }

  return SVN_NO_ERROR;
}

/* An #svn_branch_txn_t method. */
static svn_error_t *
branch_txn_new_eid(svn_branch_txn_t *txn,
                   svn_branch_eid_t *eid_p,
                   apr_pool_t *scratch_pool)
{
  int eid = (txn->first_eid < 0) ? txn->first_eid - 1 : -2;

  txn->first_eid = eid;
  if (eid_p)
    *eid_p = eid;
  return SVN_NO_ERROR;
}

/* An #svn_branch_txn_t method. */
static svn_error_t *
branch_txn_open_branch(svn_branch_txn_t *txn,
                       svn_branch_state_t **new_branch_p,
                       svn_branch_rev_bid_t *predecessor,
                       const char *outer_branch_id,
                       int outer_eid,
                       int root_eid,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  const char *new_branch_id;
  svn_branch_state_t *new_branch;

  /* if the subbranch already exists, just return it */
  new_branch_id
    = svn_branch_id_nest(outer_branch_id, outer_eid, result_pool);
  new_branch
    = svn_branch_txn_get_branch_by_id(txn, new_branch_id, scratch_pool);
  if (new_branch)
    {
      SVN_ERR_ASSERT(root_eid == svn_branch_root_eid(new_branch));
    }
  else
    {
      new_branch = svn_branch_txn_add_new_branch(txn,
                                                 new_branch_id,
                                                 predecessor,
                                                 root_eid, scratch_pool);
    }

  if (new_branch_p)
    *new_branch_p = new_branch;
  return SVN_NO_ERROR;
}

/* An #svn_branch_txn_t method. */
static svn_error_t *
branch_txn_branch(svn_branch_txn_t *txn,
                  svn_branch_state_t **new_branch_p,
                  svn_branch_rev_bid_eid_t *from,
                  const char *outer_branch_id,
                  int outer_eid,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  svn_branch_rev_bid_t *predecessor;
  svn_branch_state_t *new_branch;
  svn_branch_state_t *from_branch;
  svn_element_tree_t *from_subtree;
  const char *new_branch_id;

  SVN_ERR(branch_in_rev_or_txn(&from_branch, from, txn, scratch_pool));
  /* Source branch must exist */
  if (! from_branch)
    {
      return svn_error_createf(SVN_ERR_BRANCHING, NULL,
                               _("Cannot branch from r%ld %s e%d: "
                                 "branch does not exist"),
                               from->rev, from->bid, from->eid);
    }

  from_subtree = svn_branch_get_element_tree_at_eid(from_branch, from->eid,
                                                    scratch_pool);
  /* Source element must exist */
  if (! from_subtree)
    {
      return svn_error_createf(SVN_ERR_BRANCHING, NULL,
                               _("Cannot branch from r%ld %s e%d: "
                                 "element does not exist"),
                               from->rev, from->bid, from->eid);
    }

  new_branch_id
    = svn_branch_id_nest(outer_branch_id, outer_eid, result_pool);
  predecessor = svn_branch_rev_bid_create(from->rev, from->bid, scratch_pool);
  new_branch = svn_branch_txn_add_new_branch(txn,
                                             new_branch_id,
                                             predecessor,
                                             from->eid, scratch_pool);

  /* Populate the mapping from the 'from' source */
  SVN_ERR(svn_branch_instantiate_elements(new_branch, from_subtree,
                                          scratch_pool));

  if (new_branch_p)
    *new_branch_p = new_branch;
  return SVN_NO_ERROR;
}

/* An #svn_branch_txn_t method. */
static svn_error_t *
branch_txn_sequence_point(svn_branch_txn_t *txn,
                          apr_pool_t *scratch_pool)
{
  int i;

  /* purge elements in each branch */
  for (i = 0; i < txn->priv->branches->nelts; i++)
    {
      svn_branch_state_t *b
        = APR_ARRAY_IDX(txn->priv->branches, i, void *);

      SVN_ERR(svn_branch_state_purge(b, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/*
 * ========================================================================
 * Branch Txn Object
 * ========================================================================
 */

svn_error_t *
svn_branch_txn_new_eid(svn_branch_txn_t *txn,
                        int *new_eid_p,
                        apr_pool_t *scratch_pool)
{
  SVN_ERR(txn->vtable->new_eid(txn,
                               new_eid_p,
                               txn->vtable->vpriv.scratch_pool));
  svn_pool_clear(txn->vtable->vpriv.scratch_pool);  /* ### assumes no recursion */
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch_txn_open_branch(svn_branch_txn_t *txn,
                           svn_branch_state_t **new_branch_p,
                           svn_branch_rev_bid_t *predecessor,
                           const char *outer_branch_id,
                           int outer_eid,
                           int root_eid,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  SVN_ERR(txn->vtable->open_branch(txn,
                                   new_branch_p,
                                   predecessor, outer_branch_id, outer_eid,
                                   root_eid, result_pool,
                                   txn->vtable->vpriv.scratch_pool));
  svn_pool_clear(txn->vtable->vpriv.scratch_pool);  /* ### assumes no recursion */
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch_txn_branch(svn_branch_txn_t *txn,
                      svn_branch_state_t **new_branch_p,
                      svn_branch_rev_bid_eid_t *from,
                      const char *outer_branch_id,
                      int outer_eid,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  SVN_ERR(txn->vtable->branch(txn,
                              new_branch_p,
                              from, outer_branch_id, outer_eid, result_pool,
                              txn->vtable->vpriv.scratch_pool));
  svn_pool_clear(txn->vtable->vpriv.scratch_pool);  /* ### assumes no recursion */
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch_txn_sequence_point(svn_branch_txn_t *txn,
                              apr_pool_t *scratch_pool)
{
  SVN_ERR(txn->vtable->sequence_point(txn,
                                      txn->vtable->vpriv.scratch_pool));
  svn_pool_clear(txn->vtable->vpriv.scratch_pool);  /* ### assumes no recursion */
  return SVN_NO_ERROR;
}

svn_branch_txn_t *
svn_branch_txn_create(const svn_branch_txn_vtable_t *vtable,
                      svn_cancel_func_t cancel_func,
                      void *cancel_baton,
                      apr_pool_t *result_pool)
{
  svn_branch_txn_t *txn = apr_pcalloc(result_pool, sizeof(*txn));

  txn->vtable = apr_pmemdup(result_pool, vtable, sizeof(*vtable));

  txn->vtable->vpriv.cancel_func = cancel_func;
  txn->vtable->vpriv.cancel_baton = cancel_baton;
  txn->vtable->vpriv.scratch_pool = svn_pool_create(result_pool);

#ifdef ENABLE_ORDERING_CHECK
  txn->vtable->vpriv.within_callback = FALSE;
  txn->vtable->vpriv.finished = FALSE;
  txn->vtable->vpriv.state_pool = result_pool;
#endif

  return txn;
}

/* Create a new branch txn object.
 *
 * It will have no branches.
 */
static svn_branch_txn_t *
branch_txn_create(svn_branch_repos_t *repos,
                  svn_revnum_t rev,
                  svn_revnum_t base_rev,
                  apr_pool_t *result_pool)
{
  static const svn_branch_txn_vtable_t vtable = {
    {0},
    branch_txn_new_eid,
    branch_txn_open_branch,
    branch_txn_branch,
    branch_txn_sequence_point,
  };
  svn_branch_txn_t *txn
    = svn_branch_txn_create(&vtable, NULL, NULL, result_pool);

  txn->priv = apr_pcalloc(result_pool, sizeof(*txn->priv));
  txn->repos = repos;
  txn->rev = rev;
  txn->base_rev = base_rev;
  txn->priv->branches = svn_array_make(result_pool);
  return txn;
}

/*
 * ========================================================================
 */

/*  */
static const char *
branch_finalize_bid(const char *bid,
                    int mapping_offset,
                    apr_pool_t *result_pool)
{
  const char *outer_bid;
  int outer_eid;

  svn_branch_id_unnest(&outer_bid, &outer_eid, bid, result_pool);

  if (outer_bid)
    {
      outer_bid = branch_finalize_bid(outer_bid, mapping_offset, result_pool);
    }

  if (outer_eid < -1)
    {
      outer_eid = mapping_offset - outer_eid;
    }

  return svn_branch_id_nest(outer_bid, outer_eid, result_pool);
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

  branch->bid = branch_finalize_bid(branch->bid, mapping_offset,
                                    branch_state_pool_get(branch));
  if (branch->priv->element_tree->root_eid < -1)
    {
      branch->priv->element_tree->root_eid
        = mapping_offset - branch->priv->element_tree->root_eid;
    }

  for (hi = apr_hash_first(scratch_pool, branch->priv->element_tree->e_map);
       hi; hi = apr_hash_next(hi))
    {
      int old_eid = svn_int_hash_this_key(hi);
      svn_element_content_t *element = apr_hash_this_val(hi);

      if (old_eid < -1)
        {
          int new_eid = mapping_offset - old_eid;

          svn_element_tree_set(branch->priv->element_tree, old_eid, NULL);
          svn_element_tree_set(branch->priv->element_tree, new_eid, element);
        }
      if (element->parent_eid < -1)
        {
          element->parent_eid = mapping_offset - element->parent_eid;
        }
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch_txn_finalize_eids(svn_branch_txn_t *txn,
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

  for (i = 0; i < txn->priv->branches->nelts; i++)
    {
      svn_branch_state_t *b = APR_ARRAY_IDX(txn->priv->branches, i, void *);

      SVN_ERR(branch_finalize_eids(b, mapping_offset, scratch_pool));
    }

  txn->next_eid += n_txn_eids;
  txn->first_eid = 0;
  return SVN_NO_ERROR;
}

/*
 * ========================================================================
 */

apr_array_header_t *
svn_branch_txn_get_branches(svn_branch_txn_t *txn,
                            apr_pool_t *result_pool)
{
  return apr_array_copy(result_pool, txn->priv->branches);
}

svn_branch_state_t *
svn_branch_txn_get_branch_by_id(const svn_branch_txn_t *txn,
                                const char *branch_id,
                                apr_pool_t *scratch_pool)
{
  SVN_ITER_T(svn_branch_state_t) *bi;
  svn_branch_state_t *branch = NULL;

  for (SVN_ARRAY_ITER(bi, txn->priv->branches, scratch_pool))
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

/*
 * ========================================================================
 */

static void
branch_validate_element(const svn_branch_state_t *branch,
                        int eid,
                        const svn_element_content_t *element);

/* Assert BRANCH satisfies all its invariants.
 */
static void
assert_branch_state_invariants(const svn_branch_state_t *branch,
                               apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  assert(branch->bid);
  assert(branch->txn);
  assert(branch->priv->element_tree);
  assert(branch->priv->element_tree->e_map);

  /* Validate elements in the map */
  for (hi = apr_hash_first(scratch_pool, branch->priv->element_tree->e_map);
       hi; hi = apr_hash_next(hi))
    {
      branch_validate_element(branch, svn_int_hash_this_key(hi),
                              apr_hash_this_val(hi));
    }
}

/* An #svn_branch_state_t method. */
static svn_error_t *
branch_state_copy_one(svn_branch_state_t *branch,
                      const svn_branch_rev_bid_eid_t *src_el_rev,
                      svn_branch_eid_t eid,
                      svn_branch_eid_t new_parent_eid,
                      const char *new_name,
                      const svn_element_payload_t *new_payload,
                      apr_pool_t *scratch_pool)
{
  /* New payload shall be the same as the source if NEW_PAYLOAD is null. */
  /* ### if (! new_payload)
    {
      new_payload = branch_map_get(branch, eid)->payload;
    }
   */

  return SVN_NO_ERROR;
}

/* Copy a subtree.
 *
 * Adjust TO_BRANCH and its subbranches (recursively), to reflect a copy
 * of a subtree from FROM_EL_REV to TO_PARENT_EID:TO_NAME.
 *
 * FROM_EL_REV must be an existing element. (It may be a branch root.)
 *
 * ### TODO:
 * If FROM_EL_REV is the root of a subbranch and/or contains nested
 * subbranches, also copy them ...
 * ### What shall we do with a subbranch? Make plain copies of its raw
 *     elements; make a subbranch by branching the source subbranch?
 *
 * TO_PARENT_EID must be a directory element in TO_BRANCH, and TO_NAME a
 * non-existing path in it.
 */
static svn_error_t *
copy_subtree(const svn_branch_el_rev_id_t *from_el_rev,
             svn_branch_state_t *to_branch,
             svn_branch_eid_t to_parent_eid,
             const char *to_name,
             apr_pool_t *scratch_pool)
{
  svn_element_tree_t *new_subtree;

  SVN_DBG(("cp subtree from e%d to e%d/%s",
           from_el_rev->eid, to_parent_eid, to_name));

  new_subtree = svn_branch_get_element_tree_at_eid(from_el_rev->branch,
                                                   from_el_rev->eid,
                                                   scratch_pool);

  /* copy the subtree, assigning new EIDs */
  SVN_ERR(svn_branch_map_add_subtree(to_branch, -1 /*to_eid*/,
                                     to_parent_eid, to_name,
                                     new_subtree,
                                     scratch_pool));

  return SVN_NO_ERROR;
}

/* An #svn_branch_state_t method. */
static svn_error_t *
branch_state_copy_tree(svn_branch_state_t *to_branch,
                       const svn_branch_rev_bid_eid_t *src_el_rev,
                       svn_branch_eid_t new_parent_eid,
                       const char *new_name,
                       apr_pool_t *scratch_pool)
{
  svn_branch_txn_t *txn = to_branch->txn;
  svn_branch_state_t *src_branch;
  svn_branch_el_rev_id_t *from_el_rev;

  SVN_DBG(("copy_tree(e%d -> e%d/%s)",
           src_el_rev->eid, new_parent_eid, new_name));

  SVN_ERR(branch_in_rev_or_txn(&src_branch, src_el_rev, txn, scratch_pool));
  from_el_rev = svn_branch_el_rev_id_create(src_branch, src_el_rev->eid,
                                            src_el_rev->rev, scratch_pool);
  SVN_ERR(copy_subtree(from_el_rev,
                       to_branch, new_parent_eid, new_name,
                       scratch_pool));

  return SVN_NO_ERROR;
}

/* An #svn_branch_state_t method. */
static svn_error_t *
branch_state_payload_resolve(svn_branch_state_t *branch,
                             svn_element_content_t *element,
                             apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

const char *
svn_branch_get_id(svn_branch_state_t *branch,
                  apr_pool_t *result_pool)
{
  return branch->bid;
}

int
svn_branch_root_eid(const svn_branch_state_t *branch)
{
  return branch->priv->element_tree->root_eid;
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

svn_branch_rev_bid_t *
svn_branch_rev_bid_create(svn_revnum_t rev,
                          const char *branch_id,
                          apr_pool_t *result_pool)
{
  svn_branch_rev_bid_t *id = apr_palloc(result_pool, sizeof(*id));

  id->bid = branch_id;
  id->rev = rev;
  return id;
}

svn_branch_rev_bid_t *
svn_branch_rev_bid_dup(const svn_branch_rev_bid_t *old_id,
                       apr_pool_t *result_pool)
{
  svn_branch_rev_bid_t *id;

  if (! old_id)
    return NULL;

  id = apr_pmemdup(result_pool, old_id, sizeof(*id));
  id->bid = apr_pstrdup(result_pool, old_id->bid);
  return id;
}


/*
 * ========================================================================
 * Branch mappings
 * ========================================================================
 */

const svn_element_tree_t *
svn_branch_get_element_tree(svn_branch_state_t *branch)
{
  return branch->priv->element_tree;
}

/* Validate that ELEMENT is suitable for a mapping of BRANCH:EID.
 * ELEMENT->payload may be null.
 */
static void
branch_validate_element(const svn_branch_state_t *branch,
                        int eid,
                        const svn_element_content_t *element)
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

  SVN_ERR_ASSERT_NO_RETURN(svn_element_payload_invariants(element->payload));
  if (element->payload->is_subbranch_root)
    {
      /* a subbranch root element must not be the branch root element */
      SVN_ERR_ASSERT_NO_RETURN(! IS_BRANCH_ROOT_EID(branch, eid));
    }
}

apr_hash_t *
svn_branch_get_elements(svn_branch_state_t *branch)
{
  return branch->priv->element_tree->e_map;
}

svn_element_content_t *
svn_branch_get_element(const svn_branch_state_t *branch,
                       int eid)
{
  svn_element_content_t *element;

  element = svn_element_tree_get(branch->priv->element_tree, eid);

  if (element)
    branch_validate_element(branch, eid, element);
  return element;
}

/* In BRANCH, set element EID to ELEMENT.
 *
 * If ELEMENT is null, delete element EID.
 *
 * Assume ELEMENT is already allocated with sufficient lifetime.
 */
static void
branch_map_set(svn_branch_state_t *branch,
               int eid,
               svn_element_content_t *element)
{
  apr_pool_t *map_pool = apr_hash_pool_get(branch->priv->element_tree->e_map);

  SVN_ERR_ASSERT_NO_RETURN(EID_IS_ALLOCATED(branch, eid));
  if (element)
    branch_validate_element(branch, eid, element);

  svn_element_tree_set(branch->priv->element_tree, eid, element);
  assert_branch_state_invariants(branch, map_pool);
}

/* An #svn_branch_state_t method. */
static svn_error_t *
branch_state_delete_one(svn_branch_state_t *branch,
                        svn_branch_eid_t eid,
                        apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(EID_IS_ALLOCATED(branch, eid));

  SVN_DBG(("delete(b%s e%d)",
           svn_branch_get_id(branch, scratch_pool), eid));

  branch_map_set(branch, eid, NULL);

  return SVN_NO_ERROR;
}

/* An #svn_branch_state_t method. */
static svn_error_t *
branch_state_alter(svn_branch_state_t *branch,
                   svn_branch_eid_t eid,
                   svn_branch_eid_t new_parent_eid,
                   const char *new_name,
                   const svn_element_payload_t *new_payload,
                   apr_pool_t *scratch_pool)
{
  apr_pool_t *map_pool = apr_hash_pool_get(branch->priv->element_tree->e_map);
  svn_element_content_t *element
    = svn_element_content_create(new_parent_eid, new_name, new_payload,
                                 map_pool);

  /* EID must be a valid element id */
  SVN_ERR_ASSERT(EID_IS_ALLOCATED(branch, eid));
  /* NEW_PAYLOAD must be specified, either in full or by reference */
  SVN_ERR_ASSERT(new_payload);

  /* Insert the new version */
  branch_map_set(branch, eid, element);
  return SVN_NO_ERROR;
}

svn_element_tree_t *
svn_branch_get_element_tree_at_eid(svn_branch_state_t *branch,
                                   int eid,
                                   apr_pool_t *result_pool)
{
  svn_element_tree_t *new_subtree;
  svn_element_content_t *subtree_root_element;

  SVN_BRANCH_SEQUENCE_POINT(branch);

  new_subtree = svn_element_tree_create(branch->priv->element_tree->e_map, eid,
                                        result_pool);

  /* Purge orphans */
  svn_element_tree_purge_orphans(new_subtree->e_map,
                                 new_subtree->root_eid, result_pool);

  /* Remove 'parent' and 'name' attributes from subtree root element */
  subtree_root_element
    = svn_element_tree_get(new_subtree, new_subtree->root_eid);
  svn_element_tree_set(new_subtree, new_subtree->root_eid,
                       svn_element_content_create(
                         -1, "", subtree_root_element->payload, result_pool));

  return new_subtree;
}

/* An #svn_branch_state_t method. */
static svn_error_t *
branch_state_purge(svn_branch_state_t *branch,
                   apr_pool_t *scratch_pool)
{
  svn_element_tree_purge_orphans(branch->priv->element_tree->e_map,
                                 branch->priv->element_tree->root_eid,
                                 scratch_pool);
  return SVN_NO_ERROR;
}

const char *
svn_branch_get_path_by_eid(const svn_branch_state_t *branch,
                           int eid,
                           apr_pool_t *result_pool)
{
  const char *path = "";
  svn_element_content_t *element;

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

int
svn_branch_get_eid_by_path(const svn_branch_state_t *branch,
                           const char *path,
                           apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  /* ### This is a crude, linear search */
  for (hi = apr_hash_first(scratch_pool, branch->priv->element_tree->e_map);
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

svn_error_t *
svn_branch_map_add_subtree(svn_branch_state_t *to_branch,
                           int to_eid,
                           svn_branch_eid_t new_parent_eid,
                           const char *new_name,
                           svn_element_tree_t *new_subtree,
                           apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  svn_element_content_t *new_root_content;

  /* Get a new EID for the root element, if not given. */
  if (to_eid == -1)
    {
      SVN_ERR(svn_branch_txn_new_eid(to_branch->txn, &to_eid,
                                     scratch_pool));
    }

  /* Create the new subtree root element */
  new_root_content = svn_element_tree_get(new_subtree, new_subtree->root_eid);
  SVN_ERR(branch_state_alter(to_branch, to_eid,
                             new_parent_eid, new_name,
                             new_root_content->payload,
                             scratch_pool));

  /* Process its immediate children */
  for (hi = apr_hash_first(scratch_pool, new_subtree->e_map);
       hi; hi = apr_hash_next(hi))
    {
      int this_from_eid = svn_int_hash_this_key(hi);
      svn_element_content_t *from_element = apr_hash_this_val(hi);

      if (from_element->parent_eid == new_subtree->root_eid)
        {
          svn_element_tree_t *this_subtree;

          /* Recurse. (We don't try to check whether it's a directory node,
             as we might not have the node kind in the map.) */
          this_subtree
            = svn_element_tree_create(new_subtree->e_map, this_from_eid,
                                      scratch_pool);
          SVN_ERR(svn_branch_map_add_subtree(to_branch, -1 /*to_eid*/,
                                             to_eid, from_element->name,
                                             this_subtree, scratch_pool));
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch_instantiate_elements(svn_branch_state_t *to_branch,
                                const svn_element_tree_t *elements,
                                apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  /* Instantiate all the elements of NEW_SUBTREE */
  for (hi = apr_hash_first(scratch_pool, elements->e_map);
       hi; hi = apr_hash_next(hi))
    {
      int this_eid = svn_int_hash_this_key(hi);
      svn_element_content_t *this_element = apr_hash_this_val(hi);

      branch_map_set(to_branch, this_eid,
                     svn_element_content_dup(
                       this_element,
                       apr_hash_pool_get(to_branch->priv->element_tree->e_map)));
    }

  return SVN_NO_ERROR;
}

svn_branch_state_t *
svn_branch_txn_add_new_branch(svn_branch_txn_t *txn,
                              const char *bid,
                              svn_branch_rev_bid_t *predecessor,
                              int root_eid,
                              apr_pool_t *scratch_pool)
{
  svn_branch_state_t *new_branch;

  SVN_ERR_ASSERT_NO_RETURN(root_eid != -1);

  new_branch = branch_state_create(bid, predecessor, root_eid, txn,
                                   txn->priv->branches->pool);

  SVN_ARRAY_PUSH(txn->priv->branches) = new_branch;

  return new_branch;
}

svn_error_t *
svn_branch_txn_delete_branch(svn_branch_txn_t *txn,
                             const char *bid,
                             apr_pool_t *scratch_pool)
{
  SVN_ITER_T(svn_branch_state_t) *bi;

  for (SVN_ARRAY_ITER(bi, txn->priv->branches, scratch_pool))
    {
      svn_branch_state_t *b = bi->val;

      if (strcmp(b->bid, bid) == 0)
        {
          SVN_DBG(("deleting branch b%s e%d",
                   bid, b->priv->element_tree->root_eid));
          svn_sort__array_delete(txn->priv->branches, bi->i, 1);
          break;
        }
    }
  return SVN_NO_ERROR;
}

/*
 * ========================================================================
 * Branch State Object
 * ========================================================================
 */

svn_error_t *
svn_branch_state_alter_one(svn_branch_state_t *branch,
                           svn_branch_eid_t eid,
                           svn_branch_eid_t new_parent_eid,
                           const char *new_name,
                           const svn_element_payload_t *new_payload,
                           apr_pool_t *scratch_pool)
{
  SVN_ERR(branch->vtable->alter_one(branch,
                                    eid, new_parent_eid, new_name, new_payload,
                                    branch->vtable->vpriv.scratch_pool));
  svn_pool_clear(branch->vtable->vpriv.scratch_pool);  /* ### assumes no recursion */
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch_state_copy_tree(svn_branch_state_t *branch,
                           const svn_branch_rev_bid_eid_t *src_el_rev,
                           svn_branch_eid_t new_parent_eid,
                           const char *new_name,
                           apr_pool_t *scratch_pool)
{
  SVN_ERR(branch->vtable->copy_tree(branch,
                                    src_el_rev, new_parent_eid, new_name,
                                    branch->vtable->vpriv.scratch_pool));
  svn_pool_clear(branch->vtable->vpriv.scratch_pool);  /* ### assumes no recursion */
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch_state_delete_one(svn_branch_state_t *branch,
                            svn_branch_eid_t eid,
                            apr_pool_t *scratch_pool)
{
  SVN_ERR(branch->vtable->delete_one(branch,
                                     eid,
                                     branch->vtable->vpriv.scratch_pool));
  svn_pool_clear(branch->vtable->vpriv.scratch_pool); /* ### assumes no recursion */
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch_state_purge(svn_branch_state_t *branch,
                       apr_pool_t *scratch_pool)
{
  SVN_ERR(branch->vtable->purge(branch,
                                branch->vtable->vpriv.scratch_pool));
  svn_pool_clear(branch->vtable->vpriv.scratch_pool); /* ### assumes no recursion */
  return SVN_NO_ERROR;
}

svn_branch_state_t *
svn_branch_state_create(const svn_branch_state_vtable_t *vtable,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *result_pool)
{
  svn_branch_state_t *b = apr_pcalloc(result_pool, sizeof(*b));

  b->vtable = apr_pmemdup(result_pool, vtable, sizeof(*vtable));

  b->vtable->vpriv.cancel_func = cancel_func;
  b->vtable->vpriv.cancel_baton = cancel_baton;
  b->vtable->vpriv.scratch_pool = svn_pool_create(result_pool);

#ifdef ENABLE_ORDERING_CHECK
  b->vtable->vpriv.within_callback = FALSE;
  b->vtable->vpriv.finished = FALSE;
  b->vtable->vpriv.state_pool = result_pool;
#endif

  return b;
}

/* Create a new branch state object.
 *
 * It will have no elements (not even a root element).
 */
static svn_branch_state_t *
branch_state_create(const char *bid,
                    svn_branch_rev_bid_t *predecessor,
                    int root_eid,
                    svn_branch_txn_t *txn,
                    apr_pool_t *result_pool)
{
  static const svn_branch_state_vtable_t vtable = {
    {0},
    branch_state_alter,
    branch_state_copy_one,
    branch_state_copy_tree,
    branch_state_delete_one,
    branch_state_payload_resolve,
    branch_state_purge,
  };
  svn_branch_state_t *b
    = svn_branch_state_create(&vtable, NULL, NULL, result_pool);

  b->priv = apr_pcalloc(result_pool, sizeof(*b->priv));
  b->bid = apr_pstrdup(result_pool, bid);
  b->predecessor = svn_branch_rev_bid_dup(predecessor, result_pool);
  b->txn = txn;
  b->priv->element_tree = svn_element_tree_create(NULL, root_eid, result_pool);
  assert_branch_state_invariants(b, result_pool);
  return b;
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
      "B0 root-eid 0 num-eids 1\n"
      "e0: normal -1 .\n";

  return svn_string_create(default_repos_info, result_pool);
}

/*  */
static svn_error_t *
parse_branch_line(char *bid_p,
                  int *root_eid_p,
                  int *num_eids_p,
                  svn_branch_rev_bid_t **predecessor,
                  svn_stream_t *stream,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *line;
  svn_boolean_t eof;
  int n;
  svn_revnum_t pred_rev;
  char pred_bid[1000];

  /* Read a line */
  SVN_ERR(svn_stream_readline(stream, &line, "\n", &eof, scratch_pool));
  SVN_ERR_ASSERT(!eof);

  n = sscanf(line->data, "%s root-eid %d num-eids %d from r%ld.%s",
             bid_p, root_eid_p, num_eids_p, &pred_rev, pred_bid);
  SVN_ERR_ASSERT(n == 3 || n == 5);

  if (n == 5)
    {
      *predecessor = svn_branch_rev_bid_create(pred_rev, pred_bid, result_pool);
    }
  else
    {
      *predecessor = NULL;
    }

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
                       svn_branch_txn_t *txn,
                       svn_stream_t *stream,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  char bid[1000];
  int root_eid, num_eids;
  svn_branch_rev_bid_t *predecessor;
  svn_branch_state_t *branch_state;
  int i;

  SVN_ERR(parse_branch_line(bid, &root_eid, &num_eids, &predecessor,
                            stream, scratch_pool, scratch_pool));

  branch_state = branch_state_create(bid, predecessor, root_eid, txn,
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
          svn_element_payload_t *payload;
          if (! is_subbranch)
            {
              payload = svn_element_payload_create_ref(txn->rev, bid, eid,
                                                       result_pool);
            }
          else
            {
              payload
                = svn_element_payload_create_subbranch(result_pool);
            }
          SVN_ERR(branch_state_alter(
            branch_state, eid, this_parent_eid, this_name, payload,
            scratch_pool));
        }
    }

  *new_branch = branch_state;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch_txn_parse(svn_branch_txn_t **txn_p,
                     svn_branch_repos_t *repos,
                     svn_stream_t *stream,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_branch_txn_t *txn;
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

  txn = branch_txn_create(repos, rev, rev - 1, result_pool);
  txn->first_eid = first_eid;
  txn->next_eid = next_eid;

  /* parse the branches */
  for (j = 0; j < num_branches; j++)
    {
      svn_branch_state_t *branch;

      SVN_ERR(svn_branch_state_parse(&branch, txn, stream,
                                     result_pool, scratch_pool));
      SVN_ARRAY_PUSH(txn->priv->branches) = branch;
    }

  *txn_p = txn;
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
  SVN_ITER_T(svn_element_content_t) *hi;
  const char *predecessor_str = "";

  if (branch->predecessor)
    {
      assert(SVN_IS_VALID_REVNUM(branch->predecessor->rev));
      predecessor_str = apr_psprintf(scratch_pool, " from r%ld.%s",
                                     branch->predecessor->rev,
                                     branch->predecessor->bid);
    }

  SVN_ERR(svn_stream_printf(stream, scratch_pool,
                            "%s root-eid %d num-eids %d%s\n",
                            svn_branch_get_id(branch, scratch_pool),
                            branch->priv->element_tree->root_eid,
                            apr_hash_count(branch->priv->element_tree->e_map),
                            predecessor_str));

  svn_element_tree_purge_orphans(branch->priv->element_tree->e_map,
                                 branch->priv->element_tree->root_eid,
                                 scratch_pool);

  for (SVN_HASH_ITER_SORTED(hi, branch->priv->element_tree->e_map,
                            sort_compare_items_by_eid, scratch_pool))
    {
      int eid = *(const int *)hi->key;
      svn_element_content_t *element = svn_branch_get_element(branch, eid);
      int parent_eid;
      const char *name;

      SVN_ERR_ASSERT(element);
      parent_eid = element->parent_eid;
      name = element->name[0] ? element->name : ".";
      SVN_ERR(svn_stream_printf(stream, scratch_pool,
                                "e%d: %s %d %s\n",
                                eid,
                                element ? ((! element->payload->is_subbranch_root)
                                             ? "normal" : "subbranch")
                                     : "none",
                                parent_eid, name));
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch_txn_serialize(svn_stream_t *stream,
                         svn_branch_txn_t *txn,
                         apr_pool_t *scratch_pool)
{
  SVN_ITER_T(svn_branch_state_t) *bi;

  SVN_ERR(svn_stream_printf(stream, scratch_pool,
                            "r%ld: eids %d %d "
                            "branches %d\n",
                            txn->rev,
                            txn->first_eid, txn->next_eid,
                            txn->priv->branches->nelts));

  for (SVN_ARRAY_ITER(bi, txn->priv->branches, scratch_pool))
    {
      svn_branch_state_t *branch = bi->val;

      if (branch->predecessor && branch->predecessor->rev < 0)
        {
          branch->predecessor->rev = txn->rev;
        }

      SVN_ERR(svn_branch_state_serialize(stream, bi->val, bi->iterpool));
    }
  return SVN_NO_ERROR;
}


/*
 * ========================================================================
 */

