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
#include "svn_dirent_uri.h"
#include "svn_fs.h"
#include "svn_ctype.h"

#include "private/svn_fs_private.h"
#include "private/svn_sqlite.h"

#include "fs_git.h"


static svn_error_t *
revmap_update_branch(svn_fs_t *fs,
                     svn_fs_git_fs_t *fgf,
                     git_reference *ref,
                     const git_oid *walk_oid,
                     const char *relpath,
                     svn_revnum_t *latest_rev,
                     svn_cancel_func_t cancel_func,
                     void *cancel_baton,
                     apr_pool_t *scratch_pool)
{
  const char *name = git_reference_name(ref);
  git_revwalk *revwalk = fgf->revwalk;
  int git_err;
  git_oid oid;
  svn_revnum_t last_rev = SVN_INVALID_REVNUM;
  svn_boolean_t ensured_branch = (relpath != NULL);

  /* ### TODO: Return if walk_oid is already mapped */

  if (!relpath)
    {
      const char *n = strrchr(name, '/');

      /* ### TODO: Improve algorithm */
      if (n)
        n++;
      else
        n = name;

      relpath = svn_relpath_join("branches", n, scratch_pool);
    }

  git_revwalk_reset(revwalk);
  git_revwalk_push(revwalk, walk_oid);
  git_revwalk_simplify_first_parent(revwalk);
  git_revwalk_sorting(revwalk, GIT_SORT_REVERSE);

  while (!(git_err = git_revwalk_next(&oid, revwalk)))
    {
      svn_revnum_t y_rev, rev;
      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      y_rev = *latest_rev;

      SVN_ERR(svn_fs_git__db_ensure_commit(&rev, fs, &oid,
                                           y_rev, last_rev,
                                           relpath, ref));

      if (rev > y_rev)
        {
          *latest_rev = rev;

          if (!ensured_branch)
            {
              SVN_ERR(svn_fs_git__db_branch_ensure(fs, relpath,
                                                   rev, rev,
                                                   scratch_pool));
              ensured_branch = TRUE;
            }
        }

      last_rev = rev;
    }

  if (git_err != GIT_ITEROVER)
    return svn_git__wrap_git_error();

  return SVN_NO_ERROR;
}

static svn_error_t *
revmap_update_tag(svn_fs_t *fs,
                  svn_fs_git_fs_t *fgf,
                  const char *name,
                  const git_oid *oid,
                  svn_revnum_t *latest_rev,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  apr_pool_t *scratch_pool)
{
  svn_revnum_t rev;
  const char *path;
  git_object *obj;
  git_oid walk_oid;
  char *tagname = apr_pstrdup(scratch_pool, name);

  if (!strncmp(tagname, "refs/tags/", 10))
    tagname += 10;

  {
    char *c = tagname;

    /* ### TODO: Improve algorithm */
    while (*c)
    {
      if (!svn_ctype_isprint(*c))
        *c = '_';
      else if (strchr("/\\\"<>", *c))
        *c = '_';

      c++;
    }
  }

  GIT2_ERR(git_object_lookup(&obj, fgf->repos, oid, GIT_OBJ_ANY));
  if (git_object_type(obj) != GIT_OBJ_COMMIT)
    {
      git_object *commit;
      int git_err = git_object_peel(&commit, obj,
                                    GIT_OBJ_COMMIT);

      if (!git_err)
        {
          git_object_free(obj);
          obj = commit;
        }
    }

  walk_oid = *git_object_id(obj);
  git_object_free(obj);

  SVN_ERR(svn_fs_git__db_fetch_rev(&rev, &path, fs, &walk_oid,
                                   scratch_pool, scratch_pool));

  if (!SVN_IS_VALID_REVNUM(rev))
    {
      const char *branchname;
      svn_revnum_t y_rev = *latest_rev;

      /* This commit doesn't exist on trunk or one of the branches...
         Let's create a temporary branch.

         The easiest to get 'free' path in the repository itself
         is the tag itself */

      branchname = svn_relpath_join("tags", tagname,
                                    scratch_pool);

      SVN_ERR(revmap_update_branch(fs, fgf, NULL, oid,
                                   branchname,
                                   latest_rev,
                                   cancel_func, cancel_baton,
                                   scratch_pool));

      if (*latest_rev > y_rev)
        {
          rev = *latest_rev;
          path = branchname;
        }
      else
        {
          /* The tag wasn't copied from a commit, and
             doesn't have any unique commits */
          SVN_ERR_MALFUNCTION();
        }
    }

  {
    svn_revnum_t tag_rev;

    SVN_ERR(svn_fs_git__db_tag_create(&tag_rev,
                                      fs, svn_relpath_join("tags", tagname,
                                                           scratch_pool),
                                      *latest_rev, rev, scratch_pool));

    if (tag_rev > *latest_rev)
      *latest_rev = tag_rev;
  }

  return SVN_NO_ERROR;
}

/* Baton for revmap_update_tag_cb */
typedef struct tag_update_baton_t
{
  svn_error_t *err;

  svn_fs_t *fs;
  svn_fs_git_fs_t *fgf;
  svn_revnum_t *latest_rev;

  apr_pool_t *iterpool;
  svn_cancel_func_t cancel_func;
  void *cancel_baton;

} tag_update_baton_t;

/* git_tag_foreach callback around revmap_update_tag */
static int
revmap_update_tag_cb(const char *name, git_oid *oid, void *payload)
{
  tag_update_baton_t *tub = payload;

  if (!tub->err && tub->cancel_func)
    tub->err = tub->cancel_func(tub->cancel_baton);

  if (tub->err)
    return 0;

  svn_pool_clear(tub->iterpool);

  tub->err = revmap_update_tag(tub->fs, tub->fgf,
                               name, oid, tub->latest_rev,
                               tub->cancel_func, tub->cancel_baton,
                               tub->iterpool);

  return 0;
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
  git_oid tmp_oid;

  SVN_ERR(svn_fs_git__db_youngest_rev(&youngest, fs, scratch_pool));

  if (youngest == 0)
    youngest = 1; /* We use r1 to create /trunk, /branches and /tags.
                     Let's not add other changes in the same rev */

  latest_rev = youngest;

  if (!git_repository_head_unborn(fgf->repos))
    {
      GIT2_ERR(git_repository_head(&ref, fgf->repos));

      err = revmap_update_branch(fs, fgf, ref, git_reference_target(ref),
                                 "trunk", &latest_rev,
                                 cancel_func, cancel_baton,
                                 iterpool);
      git_reference_free(ref);
      SVN_ERR(err);
    }

  GIT2_ERR(git_branch_iterator_new(&iter, fgf->repos, GIT_BRANCH_ALL));
  while (!git_branch_next(&ref, &branch_t, iter) && !err)
    {
      const git_oid *walk_oid;
      svn_pool_clear(iterpool);

      walk_oid = git_reference_target(ref);

      if (!walk_oid) {
        git_reference *rr;
        int git_err = git_reference_resolve(&rr, ref);

        if (!git_err && rr)
          {
            tmp_oid = *git_reference_target(rr);
            walk_oid = &tmp_oid;
            git_reference_free(rr);
          }
      }

      err = revmap_update_branch(fs, fgf, ref, walk_oid,
                                 NULL, &latest_rev,
                                 cancel_func, cancel_baton,
                                 iterpool);
    }

  git_branch_iterator_free(iter);

  {
    int git_err;
    tag_update_baton_t tub;

    tub.fs = fs;
    tub.fgf = fgf;
    tub.latest_rev = &latest_rev;
    tub.iterpool = iterpool;

    tub.cancel_func = cancel_func;
    tub.cancel_baton = cancel_baton;

    tub.err = NULL;

    git_err = git_tag_foreach(fgf->repos, revmap_update_tag_cb, &tub);

    if (tub.err)
      return svn_error_trace(tub.err);
    GIT2_ERR(git_err);
  }

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
