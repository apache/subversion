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
get_time (apr_time_t *tm,
          svn_fs_t *fs,
          svn_revnum_t rev,
          apr_pool_t *pool)
{
  svn_string_t *date_str;

  SVN_ERR (svn_fs_revision_prop (&date_str, fs, rev, SVN_PROP_REVISION_DATE,
                                 pool));
  if (! date_str)    
    return svn_error_createf
      (SVN_ERR_FS_GENERAL, NULL,
       _("Failed to find time on revision %ld"), rev);

  SVN_ERR (svn_time_from_cstring (tm, date_str->data, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_dated_revision (svn_revnum_t *revision,
                          svn_repos_t *repos,
                          apr_time_t tm,
                          apr_pool_t *pool)
{
  svn_revnum_t rev_mid, rev_top, rev_bot, rev_latest;
  apr_time_t this_time;
  svn_fs_t *fs = repos->fs;

  /* Initialize top and bottom values of binary search. */
  SVN_ERR (svn_fs_youngest_rev (&rev_latest, fs, pool));
  rev_bot = 0;
  rev_top = rev_latest;

  while (rev_bot <= rev_top)
    {
      rev_mid = (rev_top + rev_bot) / 2;
      SVN_ERR (get_time (&this_time, fs, rev_mid, pool));
      
      if (this_time > tm)/* we've overshot */
        {
          apr_time_t previous_time;

          if ((rev_mid - 1) < 0)
            {
              *revision = 0;
              break;
            }

          /* see if time falls between rev_mid and rev_mid-1: */
          SVN_ERR (get_time (&previous_time, fs, rev_mid - 1, pool));
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
          SVN_ERR (get_time (&next_time, fs, rev_mid + 1, pool));
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
svn_repos_get_committed_info (svn_revnum_t *committed_rev,
                              const char **committed_date,
                              const char **last_author,
                              svn_fs_root_t *root,
                              const char *path,
                              apr_pool_t *pool)
{
  svn_fs_t *fs = svn_fs_root_fs (root);

  /* ### It might be simpler just to declare that revision
     properties have char * (i.e., UTF-8) values, not arbitrary
     binary values, hmmm. */
  svn_string_t *committed_date_s, *last_author_s;
  
  /* Get the CR field out of the node's skel. */
  SVN_ERR (svn_fs_node_created_rev (committed_rev, root, path, pool));

  /* Get the date property of this revision. */
  SVN_ERR (svn_fs_revision_prop (&committed_date_s, fs, *committed_rev,
                                 SVN_PROP_REVISION_DATE, pool));

  /* Get the author property of this revision. */
  SVN_ERR (svn_fs_revision_prop (&last_author_s, fs, *committed_rev,
                                 SVN_PROP_REVISION_AUTHOR, pool));

  *committed_date = committed_date_s ? committed_date_s->data : NULL;
  *last_author = last_author_s ? last_author_s->data : NULL;
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_history (svn_fs_t *fs,
                   const char *path,
                   svn_repos_history_func_t history_func,
                   void *history_baton,
                   svn_revnum_t start,
                   svn_revnum_t end,
                   svn_boolean_t cross_copies,
                   apr_pool_t *pool)
{
  svn_fs_history_t *history;
  apr_pool_t *oldpool = svn_pool_create (pool);
  apr_pool_t *newpool = svn_pool_create (pool);
  const char *history_path;
  svn_revnum_t history_rev;
  svn_fs_root_t *root;

  /* Validate the revisions. */
  if (! SVN_IS_VALID_REVNUM (start))
    return svn_error_createf 
      (SVN_ERR_FS_NO_SUCH_REVISION, 0, 
       _("Invalid start revision %ld"), start);
  if (! SVN_IS_VALID_REVNUM (end))
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
  SVN_ERR (svn_fs_revision_root (&root, fs, end, pool));
  SVN_ERR (svn_fs_node_history (&history, root, path, oldpool));

  /* Now, we loop over the history items, calling svn_fs_history_prev(). */
  do
    {
      apr_pool_t *tmppool;

      /* Note that we have to do some crazy pool work here.  We can't
         get rid of the old history until we use it to get the new, so
         we alternate back and forth between our subpools.  */
      SVN_ERR (svn_fs_history_prev (&history, history, cross_copies, newpool));

      /* Only continue if there is further history to deal with. */
      if (! history)
        break;

      /* Fetch the location information for this history step. */
      SVN_ERR (svn_fs_history_location (&history_path, &history_rev,
                                        history, newpool));
      
      /* If this history item predates our START revision, quit
         here. */
      if (history_rev < start)
        break;

      /* Call the user-provided callback function. */
      SVN_ERR (history_func (history_baton, history_path, 
                             history_rev, newpool));

      /* We're done with the old history item, so we can clear its
         pool, and then toggle our notion of "the old pool". */
      svn_pool_clear (oldpool);
      tmppool = oldpool;
      oldpool = newpool;
      newpool = tmppool;
    }
  while (history); /* shouldn't hit this */

  svn_pool_destroy (oldpool);
  svn_pool_destroy (newpool);
  return SVN_NO_ERROR;
}

/* Compare revision numbers for sorting in decreasing order. */
static int
compare_revnums (const void *p_a, const void *p_b)
{
  svn_revnum_t a, b;
  a = *(svn_revnum_t *)p_a;
  b = *(svn_revnum_t *)p_b;

  return (a < b) ? 1 : (a > b) ? -1 : 0;
}

/* The purpose of this function is to discover if fs_path@future_rev
 * is derived from fs_path@peg_rev.  The return is placed in *is_ancestor. */

static svn_error_t *
check_ancestry_of_peg_path (svn_boolean_t *is_ancestor,
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

  lastpool = svn_pool_create (pool);
  currpool = svn_pool_create (pool);

  SVN_ERR (svn_fs_revision_root (&root, fs, future_revision, pool));

  SVN_ERR (svn_fs_node_history (&history, root, fs_path, lastpool));

  while (1)
    {
      apr_pool_t *tmppool;

      SVN_ERR (svn_fs_history_prev (&history, history, TRUE, currpool));

      if (!history)
        break;

      SVN_ERR (svn_fs_history_location (&path, &revision, history, currpool));

      if (revision <= peg_revision)
        break;

      /* Clear old pool and flip. */
      svn_pool_clear (lastpool);
      tmppool = lastpool;
      lastpool = currpool;
      currpool = tmppool;
    }

  *is_ancestor = (history && strcmp (path, fs_path) == 0);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_repos_trace_node_locations (svn_fs_t *fs,
                                apr_hash_t **locations,
                                const char *fs_path,
                                svn_revnum_t peg_revision,
                                apr_array_header_t *location_revisions_orig,
                                apr_pool_t *pool)
{
  apr_array_header_t *location_revisions;
  svn_revnum_t *revision_ptr, *revision_ptr_end;
  svn_fs_root_t *root;
  svn_fs_history_t *history;
  const char *path;
  svn_revnum_t revision;
  svn_boolean_t is_ancestor;
  apr_pool_t *lastpool, *currpool;

  /* Sanity check. */
  assert (location_revisions_orig->elt_size == sizeof(svn_revnum_t));

  *locations = apr_hash_make (pool);

  /* We flip between two pools in the second loop below. */
  lastpool = svn_pool_create (pool);
  currpool = svn_pool_create (pool);

  /* First - let's sort the array of the revisions from the greatest revision
   * downward, so it will be easier to search on. */
  location_revisions = apr_array_copy (pool, location_revisions_orig);
  qsort (location_revisions->elts, location_revisions->nelts,
         sizeof (*revision_ptr), compare_revnums);

  revision_ptr = (svn_revnum_t *)location_revisions->elts;
  revision_ptr_end = revision_ptr + location_revisions->nelts;

  /* Ignore revisions R that are younger than the peg_revisions where
     path@peg_revision is not an ancestor of path@R. */
  is_ancestor = FALSE;
  while (revision_ptr < revision_ptr_end && *revision_ptr > peg_revision)
    {
      svn_pool_clear (currpool);
      SVN_ERR (check_ancestry_of_peg_path (&is_ancestor, fs, fs_path,
                                           peg_revision, *revision_ptr,
                                           currpool));
      if (is_ancestor)
        break;
      ++revision_ptr;
    }

  SVN_ERR (svn_fs_revision_root (&root, fs,
                                 (is_ancestor ?
                                  (*revision_ptr) :
                                  peg_revision), pool));

  SVN_ERR (svn_fs_node_history (&history, root, fs_path, lastpool));

  while (revision_ptr < revision_ptr_end)
    {
      apr_pool_t *tmppool;

      SVN_ERR (svn_fs_history_prev (&history, history, TRUE, currpool));
      if (!history)
        break;

      SVN_ERR (svn_fs_history_location (&path, &revision, history, currpool));

      /* Assign the current path to all younger revisions until we reach
         the current one. */
      while ((revision_ptr < revision_ptr_end) && (*revision_ptr >= revision))
        {
          /* *revision_ptr is allocated out of pool, so we can point
             to in the hash table. */
          apr_hash_set (*locations, revision_ptr, sizeof (*revision_ptr),
                        apr_pstrdup (pool, path));
          revision_ptr++;
        }

      /* Clear last pool and switch. */
      svn_pool_clear (lastpool);
      tmppool = lastpool;
      lastpool = currpool;
      currpool = tmppool;
    }

  svn_pool_destroy (lastpool);
  svn_pool_destroy (currpool);

  return SVN_NO_ERROR;
}

