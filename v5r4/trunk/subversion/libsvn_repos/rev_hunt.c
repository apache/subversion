/* rev_hunt.c --- routines to hunt down particular fs revisions and
 *                their properties.
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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


#include <string.h>
#include "svn_private_config.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_string.h"
#include "svn_time.h"
#include "svn_sorts.h"
#include "svn_path.h"
#include "svn_props.h"
#include "repos.h"

#include <assert.h>


/* Note:  this binary search assumes that the datestamp properties on
   each revision are in chronological order.  That is if revision A >
   revision B, then A's datestamp is younger then B's datestamp.

   If some moron comes along and sets a bogus datestamp, this routine
   might not work right.

   ### todo:  you know, we *could* have svn_fs_change_rev_prop() do
   some semantic checking when it's asked to change special reserved
   svn: properties.  It could prevent such a problem. */


/* helper for svn_repos_dated_revision().

   Set *TM to the apr_time_t datestamp on revision REV in FS. */
static svn_error_t *
get_time(apr_time_t *tm,
         svn_fs_t *fs,
         svn_revnum_t rev,
         apr_pool_t *pool)
{
  svn_string_t *date_str;

  SVN_ERR(svn_fs_revision_prop(&date_str, fs, rev, SVN_PROP_REVISION_DATE,
                               pool));
  if (! date_str)    
    return svn_error_createf
      (SVN_ERR_FS_GENERAL, NULL,
       _("Failed to find time on revision %ld"), rev);

  SVN_ERR(svn_time_from_cstring(tm, date_str->data, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_dated_revision(svn_revnum_t *revision,
                         svn_repos_t *repos,
                         apr_time_t tm,
                         apr_pool_t *pool)
{
  svn_revnum_t rev_mid, rev_top, rev_bot, rev_latest;
  apr_time_t this_time;
  svn_fs_t *fs = repos->fs;

  /* Initialize top and bottom values of binary search. */
  SVN_ERR(svn_fs_youngest_rev(&rev_latest, fs, pool));
  rev_bot = 0;
  rev_top = rev_latest;

  while (rev_bot <= rev_top)
    {
      rev_mid = (rev_top + rev_bot) / 2;
      SVN_ERR(get_time(&this_time, fs, rev_mid, pool));
      
      if (this_time > tm)/* we've overshot */
        {
          apr_time_t previous_time;

          if ((rev_mid - 1) < 0)
            {
              *revision = 0;
              break;
            }

          /* see if time falls between rev_mid and rev_mid-1: */
          SVN_ERR(get_time(&previous_time, fs, rev_mid - 1, pool));
          if (previous_time <= tm)
            {
              *revision = rev_mid - 1;
              break;
            }

          rev_top = rev_mid - 1;
        }

      else if (this_time < tm) /* we've undershot */
        {
          apr_time_t next_time;

          if ((rev_mid + 1) > rev_latest)
            {
              *revision = rev_latest;
              break;
            }
          
          /* see if time falls between rev_mid and rev_mid+1: */
          SVN_ERR(get_time(&next_time, fs, rev_mid + 1, pool));
          if (next_time > tm)
            {
              *revision = rev_mid;
              break;
            }

          rev_bot = rev_mid + 1;
        }

      else
        {
          *revision = rev_mid;  /* exact match! */
          break;
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_get_committed_info(svn_revnum_t *committed_rev,
                             const char **committed_date,
                             const char **last_author,
                             svn_fs_root_t *root,
                             const char *path,
                             apr_pool_t *pool)
{
  svn_fs_t *fs = svn_fs_root_fs(root);

  /* ### It might be simpler just to declare that revision
     properties have char * (i.e., UTF-8) values, not arbitrary
     binary values, hmmm. */
  svn_string_t *committed_date_s, *last_author_s;
  
  /* Get the CR field out of the node's skel. */
  SVN_ERR(svn_fs_node_created_rev(committed_rev, root, path, pool));

  /* Get the date property of this revision. */
  SVN_ERR(svn_fs_revision_prop(&committed_date_s, fs, *committed_rev,
                               SVN_PROP_REVISION_DATE, pool));

  /* Get the author property of this revision. */
  SVN_ERR(svn_fs_revision_prop(&last_author_s, fs, *committed_rev,
                               SVN_PROP_REVISION_AUTHOR, pool));

  *committed_date = committed_date_s ? committed_date_s->data : NULL;
  *last_author = last_author_s ? last_author_s->data : NULL;
  
  return SVN_NO_ERROR;
}


/* Deprecated. */
svn_error_t *
svn_repos_history(svn_fs_t *fs,
                  const char *path,
                  svn_repos_history_func_t history_func,
                  void *history_baton,
                  svn_revnum_t start,
                  svn_revnum_t end,
                  svn_boolean_t cross_copies,
                  apr_pool_t *pool)
{
  return svn_repos_history2(fs, path, history_func, history_baton,
                            NULL, NULL,
                            start, end, cross_copies, pool);
}



svn_error_t *
svn_repos_history2(svn_fs_t *fs,
                   const char *path,
                   svn_repos_history_func_t history_func,
                   void *history_baton,
                   svn_repos_authz_func_t authz_read_func,
                   void *authz_read_baton,
                   svn_revnum_t start,
                   svn_revnum_t end,
                   svn_boolean_t cross_copies,
                   apr_pool_t *pool)
{
  svn_fs_history_t *history;
  apr_pool_t *oldpool = svn_pool_create(pool);
  apr_pool_t *newpool = svn_pool_create(pool);
  const char *history_path;
  svn_revnum_t history_rev;
  svn_fs_root_t *root;

  /* Validate the revisions. */
  if (! SVN_IS_VALID_REVNUM(start))
    return svn_error_createf 
      (SVN_ERR_FS_NO_SUCH_REVISION, 0, 
       _("Invalid start revision %ld"), start);
  if (! SVN_IS_VALID_REVNUM(end))
    return svn_error_createf 
      (SVN_ERR_FS_NO_SUCH_REVISION, 0, 
       _("Invalid end revision %ld"), end);

  /* Ensure that the input is ordered. */
  if (start > end)
    {
      svn_revnum_t tmprev = start;
      start = end;
      end = tmprev;
    }

  /* Get a revision root for END, and an initial HISTORY baton.  */
  SVN_ERR(svn_fs_revision_root(&root, fs, end, pool));

  if (authz_read_func)
    {
      svn_boolean_t readable;
      SVN_ERR(authz_read_func(&readable, root, path,
                              authz_read_baton, pool));
      if (! readable)
        return svn_error_create(SVN_ERR_AUTHZ_UNREADABLE, NULL, NULL);
    }

  SVN_ERR(svn_fs_node_history(&history, root, path, oldpool));

  /* Now, we loop over the history items, calling svn_fs_history_prev(). */
  do
    {
      /* Note that we have to do some crazy pool work here.  We can't
         get rid of the old history until we use it to get the new, so
         we alternate back and forth between our subpools.  */
      apr_pool_t *tmppool;

      SVN_ERR(svn_fs_history_prev(&history, history, cross_copies, newpool));

      /* Only continue if there is further history to deal with. */
      if (! history)
        break;

      /* Fetch the location information for this history step. */
      SVN_ERR(svn_fs_history_location(&history_path, &history_rev,
                                      history, newpool));
      
      /* If this history item predates our START revision, quit
         here. */
      if (history_rev < start)
        break;

      /* Is the history item readable?  If not, quit. */
      if (authz_read_func)
        {
          svn_boolean_t readable;
          svn_fs_root_t *history_root;
          SVN_ERR(svn_fs_revision_root(&history_root, fs,
                                       history_rev, newpool));
          SVN_ERR(authz_read_func(&readable, history_root, history_path,
                                  authz_read_baton, newpool));
          if (! readable)
            break;
        }
      
      /* Call the user-provided callback function. */
      SVN_ERR(history_func(history_baton, history_path, 
                           history_rev, newpool));

      /* We're done with the old history item, so we can clear its
         pool, and then toggle our notion of "the old pool". */
      svn_pool_clear(oldpool);
      tmppool = oldpool;
      oldpool = newpool;
      newpool = tmppool;
    }
  while (history); /* shouldn't hit this */

  svn_pool_destroy(oldpool);
  svn_pool_destroy(newpool);
  return SVN_NO_ERROR;
}


/* Helper func:  return SVN_ERR_AUTHZ_UNREADABLE if ROOT/PATH is
   unreadable. */
static svn_error_t *
check_readability(svn_fs_root_t *root,
                  const char *path,
                  svn_repos_authz_func_t authz_read_func,
                  void *authz_read_baton,                          
                  apr_pool_t *pool)
{
  svn_boolean_t readable;
  SVN_ERR(authz_read_func(&readable, root, path, authz_read_baton, pool));
  if (! readable)
    return svn_error_create(SVN_ERR_AUTHZ_UNREADABLE, NULL,
                            _("Unreadable path encountered; access denied"));
  return SVN_NO_ERROR;
}


/* The purpose of this function is to discover if fs_path@future_rev
 * is derived from fs_path@peg_rev.  The return is placed in *is_ancestor. */

static svn_error_t *
check_ancestry_of_peg_path(svn_boolean_t *is_ancestor,
                           svn_fs_t *fs,
                           const char *fs_path,
                           svn_revnum_t peg_revision,
                           svn_revnum_t future_revision,
                           apr_pool_t *pool)
{
  svn_fs_root_t *root;
  svn_fs_history_t *history;
  const char *path;
  svn_revnum_t revision;
  apr_pool_t *lastpool, *currpool;

  lastpool = svn_pool_create(pool);
  currpool = svn_pool_create(pool);

  SVN_ERR(svn_fs_revision_root(&root, fs, future_revision, pool));

  SVN_ERR(svn_fs_node_history(&history, root, fs_path, lastpool));

  /* Since paths that are different according to strcmp may still be
     equivalent (due to number of consecutive slashes and the fact that
     "" is the same as "/"), we get the "canonical" path in the first
     iteration below so that the comparison after the loop will work
     correctly. */
  fs_path = NULL;

  while (1)
    {
      apr_pool_t *tmppool;

      SVN_ERR(svn_fs_history_prev(&history, history, TRUE, currpool));

      if (!history)
        break;

      SVN_ERR(svn_fs_history_location(&path, &revision, history, currpool));

      if (!fs_path)
        fs_path = apr_pstrdup(pool, path);

      if (revision <= peg_revision)
        break;

      /* Clear old pool and flip. */
      svn_pool_clear(lastpool);
      tmppool = lastpool;
      lastpool = currpool;
      currpool = tmppool;
    }

  /* We must have had at least one iteration above where we
     reassigned fs_path. Else, the path wouldn't have existed at
     future_revision and svn_fs_history would have thrown. */
  assert(fs_path != NULL);
     
  *is_ancestor = (history && strcmp(path, fs_path) == 0);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_trace_node_locations(svn_fs_t *fs,
                               apr_hash_t **locations,
                               const char *fs_path,
                               svn_revnum_t peg_revision,
                               apr_array_header_t *location_revisions_orig,
                               svn_repos_authz_func_t authz_read_func,
                               void *authz_read_baton,
                               apr_pool_t *pool)
{
  apr_array_header_t *location_revisions;
  svn_revnum_t *revision_ptr, *revision_ptr_end;
  svn_fs_root_t *root;
  const char *path;
  svn_revnum_t revision;
  svn_boolean_t is_ancestor;
  apr_pool_t *lastpool, *currpool;
  const svn_fs_id_t *id;

  /* Sanity check. */
  assert(location_revisions_orig->elt_size == sizeof(svn_revnum_t));

  /* Ensure that FS_PATH is absolute, because our path-math below will
     depend on that being the case.  */
  if (*fs_path != '/')
    fs_path = apr_pstrcat(pool, "/", fs_path, NULL);

  /* Another sanity check. */
  if (authz_read_func)
    {
      svn_fs_root_t *peg_root;
      SVN_ERR(svn_fs_revision_root(&peg_root, fs, peg_revision, pool));
      SVN_ERR(check_readability(peg_root, fs_path,
                                authz_read_func, authz_read_baton, pool));
    }

  *locations = apr_hash_make(pool);

  /* We flip between two pools in the second loop below. */
  lastpool = svn_pool_create(pool);
  currpool = svn_pool_create(pool);

  /* First - let's sort the array of the revisions from the greatest revision
   * downward, so it will be easier to search on. */
  location_revisions = apr_array_copy(pool, location_revisions_orig);
  qsort(location_revisions->elts, location_revisions->nelts,
        sizeof(*revision_ptr), svn_sort_compare_revisions);

  revision_ptr = (svn_revnum_t *)location_revisions->elts;
  revision_ptr_end = revision_ptr + location_revisions->nelts;

  /* Ignore revisions R that are younger than the peg_revisions where
     path@peg_revision is not an ancestor of path@R. */
  is_ancestor = FALSE;
  while (revision_ptr < revision_ptr_end && *revision_ptr > peg_revision)
    {
      svn_pool_clear(currpool);
      SVN_ERR(check_ancestry_of_peg_path(&is_ancestor, fs, fs_path,
                                         peg_revision, *revision_ptr,
                                         currpool));
      if (is_ancestor)
        break;
      ++revision_ptr;
    }

  revision = is_ancestor ? *revision_ptr : peg_revision;
  path = fs_path;
  if (authz_read_func)
    {
      SVN_ERR(svn_fs_revision_root(&root, fs, revision, pool));
      SVN_ERR(check_readability(root, fs_path, authz_read_func,
                                authz_read_baton, pool));
    }

  while (revision_ptr < revision_ptr_end)
    {
      apr_pool_t *tmppool;
      svn_fs_root_t *croot;
      svn_revnum_t crev, srev;
      const char *cpath, *spath, *remainder;

      /* Find the target of the innermost copy relevant to path@revision.
         The copy may be of path itself, or of a parent directory. */
      SVN_ERR(svn_fs_revision_root(&root, fs, revision, currpool));
      SVN_ERR(svn_fs_closest_copy(&croot, &cpath, root, path, currpool));
      if (! croot)
        break;

      if (authz_read_func)
        {
          svn_boolean_t readable;
          svn_fs_root_t *tmp_root;

          SVN_ERR(svn_fs_revision_root(&tmp_root, fs, revision, currpool));
          SVN_ERR(authz_read_func(&readable, tmp_root, path,
                                  authz_read_baton, currpool));
          if (! readable)
            {
              return SVN_NO_ERROR;
            }
        }

      /* Assign the current path to all younger revisions until we reach
         the copy target rev. */
      crev = svn_fs_revision_root_revision(croot);
      while ((revision_ptr < revision_ptr_end) && (*revision_ptr >= crev))
        {
          /* *revision_ptr is allocated out of pool, so we can point
             to in the hash table. */
          apr_hash_set(*locations, revision_ptr, sizeof(*revision_ptr),
                       apr_pstrdup(pool, path));
          revision_ptr++;
        }

      /* Follow the copy to its source.  Ignore all revs between the
         copy target rev and the copy source rev (non-inclusive). */
      SVN_ERR(svn_fs_copied_from(&srev, &spath, croot, cpath, currpool));
      while ((revision_ptr < revision_ptr_end) && (*revision_ptr > srev))
        revision_ptr++;

      /* Ultimately, it's not the path of the closest copy's source
         that we care about -- it's our own path's location in the
         copy source revision.  So we'll tack the relative path that
         expresses the difference between the copy destination and our
         path in the copy revision onto the copy source path to
         determine this information.  

         In other words, if our path is "/branches/my-branch/foo/bar",
         and we know that the closest relevant copy was a copy of
         "/trunk" to "/branches/my-branch", then that relative path
         under the copy destination is "/foo/bar".  Tacking that onto
         the copy source path tells us that our path was located at
         "/trunk/foo/bar" before the copy.
      */
      remainder = (strcmp(cpath, path) == 0) ? "" :
        svn_path_is_child(cpath, path, currpool);
      path = svn_path_join(spath, remainder, currpool);
      revision = srev;

      /* Clear last pool and switch. */
      svn_pool_clear(lastpool);
      tmppool = lastpool;
      lastpool = currpool;
      currpool = tmppool;
    }

  /* There are no copies relevant to path@revision.  So any remaining
     revisions either predate the creation of path@revision or have
     the node existing at the same path.  We will look up path@lrev
     for each remaining location-revision and make sure it is related
     to path@revision. */
  SVN_ERR(svn_fs_revision_root(&root, fs, revision, currpool));
  SVN_ERR(svn_fs_node_id(&id, root, path, pool));
  while (revision_ptr < revision_ptr_end)
    {
      svn_node_kind_t kind;
      const svn_fs_id_t *lrev_id;

      svn_pool_clear(currpool);
      SVN_ERR(svn_fs_revision_root(&root, fs, *revision_ptr, currpool));
      SVN_ERR(svn_fs_check_path(&kind, root, path, currpool));
      if (kind == svn_node_none)
        break;
      SVN_ERR(svn_fs_node_id(&lrev_id, root, path, currpool));
      if (! svn_fs_check_related(id, lrev_id))
        break;

      /* The node exists at the same path; record that and advance. */
      apr_hash_set(*locations, revision_ptr, sizeof(*revision_ptr),
                   apr_pstrdup(pool, path));
      revision_ptr++;
    }

  /* Ignore any remaining location-revisions; they predate the
     creation of path@revision. */

  svn_pool_destroy(lastpool);
  svn_pool_destroy(currpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos_get_file_revs(svn_repos_t *repos,
                        const char *path,
                        svn_revnum_t start,
                        svn_revnum_t end,
                        svn_repos_authz_func_t authz_read_func,
                        void *authz_read_baton,
                        svn_repos_file_rev_handler_t handler,
                        void *handler_baton,
                        apr_pool_t *pool)
{
  apr_pool_t *iter_pool, *last_pool;
  svn_fs_history_t *history;
  apr_array_header_t *revnums = apr_array_make(pool, 0,
                                               sizeof(svn_revnum_t));
  apr_array_header_t *paths = apr_array_make(pool, 0, sizeof(char *));
  apr_hash_t *last_props;
  svn_fs_root_t *root, *last_root;
  const char *last_path;
  int i;
  svn_node_kind_t kind;

  /* We switch betwwen two pools while looping, since we need information from
     the last iteration to be available. */
  iter_pool = svn_pool_create(pool);
  last_pool = svn_pool_create(pool);

  /* Open revision root for path@end. */
  /* ### Can we use last_pool for this? How long does the history
     object need the root? */
  SVN_ERR(svn_fs_revision_root(&root, repos->fs, end, pool));

  /* The path had better be a file in this revision. This avoids calling
     the callback before reporting an uglier error below. */
  SVN_ERR(svn_fs_check_path(&kind, root, path, pool));
  if (kind != svn_node_file)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FILE, NULL, _("'%s' is not a file"), path);

  /* Open a history object. */
  SVN_ERR(svn_fs_node_history(&history, root, path, last_pool));
  
  /* Get the revisions we are interested in. */
  while (1)
    {
      const char* rev_path;
      svn_revnum_t rev;
      apr_pool_t *tmp_pool;

      svn_pool_clear(iter_pool);

      SVN_ERR(svn_fs_history_prev(&history, history, TRUE, iter_pool));
      if (!history)
        break;
      SVN_ERR(svn_fs_history_location(&rev_path, &rev, history, iter_pool));
      if (authz_read_func)
        {
          svn_boolean_t readable;
          svn_fs_root_t *tmp_root;

          SVN_ERR(svn_fs_revision_root(&tmp_root, repos->fs, rev, iter_pool));
          SVN_ERR(authz_read_func(&readable, tmp_root, rev_path,
                                  authz_read_baton, iter_pool));
          if (! readable)
            {
              break;
            }
        }
      *(svn_revnum_t*) apr_array_push(revnums) = rev;
      *(char **) apr_array_push(paths) = apr_pstrdup(pool, rev_path);
      if (rev <= start)
        break;

      /* Swap pools. */
      tmp_pool = iter_pool;
      iter_pool = last_pool;
      last_pool = tmp_pool;
    }

  /* We must have at least one revision to get. */
  assert(revnums->nelts > 0);

  /* We want the first txdelta to be against the empty file. */
  last_root = NULL;
  last_path = NULL;

  /* Create an empty hash table for the first property diff. */
  last_props = apr_hash_make(last_pool);

  /* Walk through the revisions in chronological order. */
  for (i = revnums->nelts; i > 0; --i)
    {
      svn_revnum_t rev = APR_ARRAY_IDX(revnums, i - 1, svn_revnum_t);
      const char *rev_path = APR_ARRAY_IDX(paths, i - 1, const char *);
      apr_hash_t *rev_props;
      apr_hash_t *props;
      apr_array_header_t *prop_diffs;
      svn_txdelta_stream_t *delta_stream;
      svn_txdelta_window_handler_t delta_handler = NULL;
      void *delta_baton = NULL;
      apr_pool_t *tmp_pool;  /* For swapping */
      svn_boolean_t contents_changed;

      svn_pool_clear(iter_pool);

      /* Get the revision properties. */
      SVN_ERR(svn_fs_revision_proplist(&rev_props, repos->fs,
                                       rev, iter_pool));

      /* Open the revision root. */
      SVN_ERR(svn_fs_revision_root(&root, repos->fs, rev, iter_pool));

      /* Get the file's properties for this revision and compute the diffs. */
      SVN_ERR(svn_fs_node_proplist(&props, root, rev_path, iter_pool));
      SVN_ERR(svn_prop_diffs(&prop_diffs, props, last_props, pool));

      /* Check if the contents changed. */
      /* Special case: In the first revision, we always provide a delta. */
      if (last_root)
        SVN_ERR(svn_fs_contents_changed(&contents_changed,
                                        last_root, last_path,
                                        root, rev_path, iter_pool));
      else
        contents_changed = TRUE;

      /* We have all we need, give to the handler. */
      SVN_ERR(handler(handler_baton, rev_path, rev, rev_props,
                      contents_changed ? &delta_handler : NULL,
                      contents_changed ? &delta_baton : NULL,
                      prop_diffs, iter_pool));

      /* Compute and send delta if client asked for it.
         Note that this was initialized to NULL, so if !contents_changed,
         no deltas will be computed. */
      if (delta_handler)
        {
          /* Get the content delta. */
          SVN_ERR(svn_fs_get_file_delta_stream(&delta_stream,
                                               last_root, last_path,
                                               root, rev_path,
                                               iter_pool));
          /* And send. */
          SVN_ERR(svn_txdelta_send_txstream(delta_stream,
                                            delta_handler, delta_baton,
                                            iter_pool));
        }

      /* Remember root, path and props for next iteration. */
      last_root = root;
      last_path = rev_path;
      last_props = props;

      /* Swap the pools. */
      tmp_pool = iter_pool;
      iter_pool = last_pool;
      last_pool = tmp_pool;
    }

  svn_pool_destroy(last_pool);
  svn_pool_destroy(iter_pool);

  return SVN_NO_ERROR;
}
