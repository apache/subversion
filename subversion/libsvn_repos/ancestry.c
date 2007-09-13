/* ancestry.c : ancestor traversing
 *
 * ====================================================================
 * Copyright (c) 2000-2007 CollabNet.  All rights reserved.
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


/*** Includes. ***/

#include "svn_private_config.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_repos.h"
#include "svn_mergeinfo.h"
#include "svn_config.h"

#include "repos.h"


/*** Structures. ***/
struct path_info
{
  svn_stringbuf_t *path;
  svn_revnum_t history_rev;
  svn_boolean_t done;
  svn_boolean_t first_time;

  /* If possible, we like to keep open the history object for each path,
     since it avoids needed to open and close it many times as we walk
     backwards in time.  To do so we need two pools, so that we can clear
     one each time through.  If we're not holding the history open for
     this path then these three pointers will be NULL. */
  svn_fs_history_t *hist;
  apr_pool_t *newpool;
  apr_pool_t *oldpool;
};



/*** Ancestry walking. ***/

static svn_error_t *
get_merged_rev_mergeinfo(apr_hash_t **mergeinfo,
                         svn_fs_t *fs,
                         const char *path,
                         svn_revnum_t rev,
                         apr_pool_t *pool)
{
  *mergeinfo = apr_hash_make(pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
walk_merged_history(apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos__walk_ancestry(const char *end_path,
                         svn_fs_t *fs,
                         svn_revnum_t start,
                         svn_revnum_t end,
                         svn_boolean_t include_merges,
                         svn_boolean_t stop_on_copy,
                         const svn_repos__ancestry_callbacks_t *callbacks,
                         void *callbacks_baton,
                         svn_repos_authz_func_t authz_read_func,
                         void *authz_read_baton,
                         apr_pool_t *pool)
{
  svn_fs_history_t *history;
  svn_fs_root_t *root;
  svn_node_kind_t kind;
  apr_pool_t *iterpool, *lastpool;

  iterpool = svn_pool_create(pool);
  lastpool = svn_pool_create(pool);

  SVN_ERR(svn_fs_revision_root(&root, fs, end, lastpool));
  SVN_ERR(svn_fs_check_path(&kind, root, end_path, lastpool));
  if (kind != svn_node_file)
    return svn_error_createf(SVN_ERR_FS_NOT_FILE, NULL,
      _("'%s' is not a file in revision %ld"), end_path, end);

  SVN_ERR(svn_fs_node_history(&history, root, end_path, lastpool));

  while (1)
    {
      apr_pool_t *tmp_pool;
      svn_revnum_t rev;
      const char *path;
      svn_boolean_t halt;
      svn_boolean_t merging_rev = FALSE;
      apr_hash_t *mergeinfo;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_fs_history_prev(&history, history, !stop_on_copy, iterpool));
      if (!history)
        break;
      SVN_ERR(svn_fs_history_location(&path, &rev, history, iterpool));

      if (authz_read_func)
        {
          svn_boolean_t readable;
          svn_fs_root_t *tmp_root;

          SVN_ERR(svn_fs_revision_root(&tmp_root, fs, rev, iterpool));
          SVN_ERR(authz_read_func(&readable, tmp_root, path, authz_read_baton,
                                  iterpool));
          if (!readable)
            break;
        }

       /* Check for merge revs here, so that we can tell the callback. */
      if (include_merges)
        {
          SVN_ERR(get_merged_rev_mergeinfo(&mergeinfo, fs, path, rev, iterpool));

          if (apr_hash_count(mergeinfo) > 0)
            {
              /* First, check branchingness. */
              merging_rev = TRUE;
            }
        }

      SVN_ERR(callbacks->found_ancestor(callbacks_baton, path, rev, merging_rev,
                                        &halt, iterpool));

      if (include_merges && merging_rev)
        SVN_ERR(walk_merged_history(iterpool));

      if (halt || rev <= start)
        break;

      tmp_pool = iterpool;
      iterpool = lastpool;
      lastpool = tmp_pool;
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
