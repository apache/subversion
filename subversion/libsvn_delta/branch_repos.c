/*
 * branch_repos.c : Element-Based Branching and Move Tracking.
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
#include "svn_iter.h"

#include "private/svn_branch_repos.h"
#include "svn_private_config.h"


/* Per-repository branching info.
 */
struct svn_branch_repos_t
{
  /* Array of (svn_branch_revision_root_t *), indexed by revision number. */
  apr_array_header_t *rev_roots;

  /* The pool in which this object lives. */
  apr_pool_t *pool;
};


svn_branch_repos_t *
svn_branch_repos_create(apr_pool_t *result_pool)
{
  svn_branch_repos_t *repos = apr_pcalloc(result_pool, sizeof(*repos));

  repos->rev_roots = svn_array_make(result_pool);
  repos->pool = result_pool;
  return repos;
}

svn_error_t *
svn_branch_repos_add_revision(svn_branch_repos_t *repos,
                              svn_branch_txn_t *rev_root)
{
  APR_ARRAY_PUSH(repos->rev_roots, void *) = rev_root;

  return SVN_NO_ERROR;
}

struct svn_branch_txn_t *
svn_branch_repos_get_revision(const svn_branch_repos_t *repos,
                              svn_revnum_t revnum)
{
  assert(revnum < repos->rev_roots->nelts);
  return svn_array_get(repos->rev_roots, revnum);
}

svn_branch_txn_t *
svn_branch_repos_get_base_revision_root(svn_branch_txn_t *rev_root)
{
  return svn_branch_repos_get_revision(rev_root->repos, rev_root->base_rev);
}

svn_error_t *
svn_branch_repos_get_branch_by_id(svn_branch_state_t **branch_p,
                                  const svn_branch_repos_t *repos,
                                  svn_revnum_t revnum,
                                  const char *branch_id,
                                  apr_pool_t *scratch_pool)
{
  svn_branch_txn_t *rev_root;

  if (revnum < 0 || revnum >= repos->rev_roots->nelts)
    return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                             _("No such revision %ld"), revnum);

  rev_root = svn_branch_repos_get_revision(repos, revnum);
  *branch_p = svn_branch_txn_get_branch_by_id(rev_root, branch_id,
                                              scratch_pool);
  if (! *branch_p)
    return svn_error_createf(SVN_ERR_BRANCHING, NULL,
                             _("Branch %s not found in r%ld"),
                             branch_id, revnum);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_branch_repos_find_el_rev_by_id(svn_branch_el_rev_id_t **el_rev_p,
                                   const svn_branch_repos_t *repos,
                                   svn_revnum_t revnum,
                                   const char *branch_id,
                                   int eid,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  svn_branch_el_rev_id_t *el_rev = apr_palloc(result_pool, sizeof(*el_rev));
  svn_element_content_t *element;

  el_rev->rev = revnum;
  SVN_ERR(svn_branch_repos_get_branch_by_id(&el_rev->branch,
                                            repos, revnum, branch_id,
                                            scratch_pool));
  SVN_ERR(svn_branch_state_get_element(el_rev->branch, &element,
                                       eid, scratch_pool));
  if (element)
    {
      el_rev->eid = eid;
    }
  else
    {
      el_rev->eid = -1;
    }
  *el_rev_p = el_rev;
  return SVN_NO_ERROR;
}

