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

/* Return in *MERGEINFO the difference between PATH at REV and PATH at REV-1 */
static svn_error_t *
get_merged_rev_mergeinfo(apr_hash_t **mergeinfo,
                         svn_fs_t *fs,
                         const char *path,
                         svn_revnum_t rev,
                         apr_pool_t *pool)
{
  apr_hash_t *curr_mergeinfo, *prev_mergeinfo, *changed, *deleted;

  SVN_ERR(svn_repos__get_path_mergeinfo(&curr_mergeinfo, fs, path, rev, pool));
  SVN_ERR(svn_repos__get_path_mergeinfo(&prev_mergeinfo, fs, path, rev - 1,
                                        pool));
  SVN_ERR(svn_mergeinfo_diff(&deleted, &changed, prev_mergeinfo, curr_mergeinfo,
                             svn_rangelist_ignore_inheritance, pool));
  SVN_ERR(svn_mergeinfo_merge(&changed, deleted,
                              svn_rangelist_equal_inheritance, pool));

  *mergeinfo = changed;

  return SVN_NO_ERROR;
}

/* Walk a single RANGE of revisions at PATH. */
static svn_error_t *
walk_range(const char *path,
           svn_merge_range_t *range,
           svn_fs_t *fs,
           const svn_repos__ancestry_callbacks_t *callbacks,
           void *callbacks_baton,
           svn_repos_authz_func_t authz_read_func,
           void *authz_read_baton,
           apr_pool_t *pool)
{
  svn_merge_range_t tmp_range = *range;
  apr_pool_t *iterpool = svn_pool_create(pool);
  
  /* The main problem here is that we know we should look at PATH everywhere
     in range, examining every revision can get expensive.  However, we can't
     just use the history object, because the note at PATH could be replaced
     in any given revision.  So, we compromise... */
  do
    {
      svn_fs_root_t *root, *copy_root;
      svn_revnum_t copy_rev;
      const char *copy_path;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_fs_revision_root(&root, fs, tmp_range.end, iterpool));
      SVN_ERR(svn_fs_closest_copy(&copy_root, &copy_path, root, path,
                                  iterpool));

      /* If we don't have a copy, we know the entire range is for the same node.
         If we do have a copy, see if it is older than the beginning of the
         range. */
      if (copy_root == NULL)
        {
          copy_rev = tmp_range.start;
        }
      else 
        {
          copy_rev = svn_fs_revision_root_revision(copy_root);
          if (copy_rev < tmp_range.start)
            copy_rev = tmp_range.start;
        }

      SVN_ERR(svn_repos__walk_ancestry(path, fs, copy_rev, tmp_range.end, TRUE,
                                       FALSE, callbacks, callbacks_baton,
                                       authz_read_func, authz_read_baton,
                                       iterpool));

      tmp_range.end = copy_rev - 1;
    }
  while (tmp_range.start <= tmp_range.end);

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Walk merged history starting from PATH at REV. */
static svn_error_t *
walk_merged_history(const char *path,
                    svn_revnum_t rev,
                    svn_fs_t *fs,
                    apr_hash_t *mergeinfo_diff,
                    const svn_repos__ancestry_callbacks_t *callbacks,
                    void *callbacks_baton,
                    svn_repos_authz_func_t authz_read_func,
                    void *authz_read_baton,
                    apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  /* Determine the source of the merge. */
  for (hi = apr_hash_first(pool, mergeinfo_diff); hi; hi=apr_hash_next(hi))
    {
      apr_array_header_t *rangelist;
      const char *merged_path;
      int i;

      apr_hash_this(hi, (void *) &merged_path, NULL, (void *) &rangelist);
      
      for (i = 0; i < rangelist->nelts; i++)
        {
          svn_merge_range_t *range = APR_ARRAY_IDX(rangelist, i,
                                                   svn_merge_range_t *);

          SVN_ERR(walk_range(merged_path, range, fs, callbacks, callbacks_baton,
                             authz_read_func, authz_read_baton, pool));
        }
    }

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
  apr_pool_t *iterpool, *lastpool;

  iterpool = svn_pool_create(pool);
  lastpool = svn_pool_create(pool);

  SVN_ERR(svn_fs_revision_root(&root, fs, end, lastpool));
  SVN_ERR(svn_fs_node_history(&history, root, end_path, lastpool));

  while (1)
    {
      apr_pool_t *tmp_pool;
      svn_revnum_t rev;
      const char *path;
      svn_boolean_t halt;

      svn_pool_clear(iterpool);

      /* Walk the history object, looking for the previous node. */
      SVN_ERR(svn_fs_history_prev(&history, history, !stop_on_copy, iterpool));
      if (!history)
        break;
      SVN_ERR(svn_fs_history_location(&path, &rev, history, iterpool));

      /* Check authorization. */
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

      /* Report the ancestor we've found. */
      if (callbacks->found_ancestor)
        SVN_ERR(callbacks->found_ancestor(callbacks_baton, path, rev, &halt,
                                          iterpool));

      if (halt || rev <= start)
        break;

       /* Check for merges */
      if (include_merges)
        {
          apr_hash_t *mergeinfo;
          svn_boolean_t merging_rev = FALSE;

          SVN_ERR(get_merged_rev_mergeinfo(&mergeinfo, fs, path, rev, iterpool));
          if (apr_hash_count(mergeinfo) > 0)
            merging_rev = TRUE;

        /* If we have a merge, notify the consumer, and walk the merged
           history. */
        if (merging_rev)
          {
            if (callbacks->found_merge)
              SVN_ERR(callbacks->found_merge(callbacks_baton, path, rev, &halt,
                                             iterpool));
            SVN_ERR(walk_merged_history(path, rev, fs, mergeinfo, callbacks,
                                        callbacks_baton, authz_read_func,
                                        authz_read_baton, iterpool));
          }
      }

      /* Swap the temporary pools. */
      tmp_pool = iterpool;
      iterpool = lastpool;
      lastpool = tmp_pool;
    }

  /* Cleanup */
  svn_pool_destroy(iterpool);
  svn_pool_destroy(lastpool);

  return SVN_NO_ERROR;
}
