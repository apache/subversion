/* gitdb.c --- manage the mapping db of the git filesystem
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "svn_pools.h"
#include "svn_fs.h"

#include "private/svn_fs_private.h"
#include "private/svn_sqlite.h"

#include "fs_git.h"

static svn_error_t *
revmap_update_branch(svn_fs_t *fs,
                     svn_fs_git_fs_t *fgf,
                     git_reference *ref,
                     svn_revnum_t *latest_rev,
                     svn_cancel_func_t cancel_func,
                     void *cancel_baton,
                     apr_pool_t *scratch_pool)
{
  const char *name = git_reference_name(ref);
  git_revwalk *revwalk = fgf->revwalk;
  int git_err;
  git_oid oid;

  git_revwalk_reset(revwalk);
  git_revwalk_push_ref(revwalk, name);
  git_revwalk_simplify_first_parent(revwalk);
  git_revwalk_sorting(revwalk, GIT_SORT_REVERSE);

  while (!(git_err = git_revwalk_next(&oid, revwalk)))
    {
      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      SVN_ERR(svn_fs_git__db_ensure_commit(fs, &oid, latest_rev, ref));
    }

  if (git_err != GIT_ITEROVER)
    return svn_fs_git__wrap_git_error();

  return SVN_NO_ERROR;
}

static svn_error_t *
revmap_update(svn_fs_t *fs,
              svn_fs_git_fs_t *fgf,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *scratch_pool)
{
  git_branch_iterator *iter;
  git_reference *ref;
  git_branch_t branch_t;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  svn_error_t *err = NULL;
  svn_revnum_t latest_rev, youngest;

  SVN_ERR(svn_fs_git__db_youngest_rev(&youngest, fs, scratch_pool));

  if (youngest == 0)
    youngest = 1; /* We use r1 to create /trunk, /branches and /tags.
                     Let's not add other changes in the same rev */

  latest_rev = youngest;

  GIT2_ERR(git_branch_iterator_new(&iter, fgf->repos, GIT_BRANCH_ALL));

  while (!git_branch_next(&ref, &branch_t, iter) && !err)
    {
      svn_pool_clear(iterpool);
      err = revmap_update_branch(fs, fgf, ref, &latest_rev,
                                 cancel_func, cancel_baton,
                                 iterpool);
    }

  git_branch_iterator_free(iter);

  if (youngest < latest_rev) {
    /* TODO: Make sqlite optimize the order a bit */
  }

  svn_pool_destroy(iterpool);

  return svn_error_trace(err);
}

svn_error_t *
svn_fs_git__revmap_update(svn_fs_t *fs,
                          svn_fs_git_fs_t *fgf,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          apr_pool_t *scratch_pool)
{
  if (!fgf->revwalk)
    GIT2_ERR(git_revwalk_new(&fgf->revwalk, fgf->repos));

  SVN_SQLITE__WITH_LOCK(revmap_update(fs, fgf,
                                      cancel_func, cancel_baton,
                                      scratch_pool),
                        fgf->sdb);

  return SVN_NO_ERROR;
}
