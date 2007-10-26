/* rev_hunt.c --- routines to hunt down particular fs revisions and
 *                their properties.
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


#include <string.h>
#include "svn_compat.h"
#include "svn_private_config.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_string.h"
#include "svn_time.h"
#include "svn_sorts.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_mergeinfo.h"
#include "repos.h"

#include <assert.h>


/* Note:  this binary search assumes that the datestamp properties on
   each revision are in chronological order.  That is if revision A >
   revision B, then A's datestamp is younger then B's datestamp.

   If someone comes along and sets a bogus datestamp, this routine
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

/* Baton for svn_repos_history2() */
struct history_cb_baton
{
  svn_repos_history_func_t history_func;
  void *history_baton;
};

/* Callback for ancestry walking in svn_repos_history2().
   This implements svn_repos__ancestry_callbacks_t.found_ancestor() */
static svn_error_t *
history_ancestor(void *baton,
                 const char *path,
                 svn_revnum_t rev,
                 apr_pool_t *pool)
{
  struct history_cb_baton *hcb = baton;
  svn_error_t *err;

  return hcb->history_func(hcb->history_baton, path, rev, pool);
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
  svn_repos__ancestry_callbacks_t walk_callbacks =
    { history_ancestor, NULL, NULL, NULL };
  struct history_cb_baton hcb;

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

  hcb.history_func = history_func;
  hcb.history_baton = history_baton;

  /* Walk the ancestry. */
  SVN_ERR(svn_repos__walk_ancestry(path, fs, start, end, FALSE,
                                   !cross_copies, &walk_callbacks,
                                   &hcb, authz_read_func, authz_read_baton,
                                   pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_deleted_rev(svn_fs_t *fs,
                      const char *path,
                      svn_revnum_t start,
                      svn_revnum_t end,
                      svn_revnum_t *deleted,
                      apr_pool_t *pool)
{
  apr_pool_t *subpool;
  svn_fs_root_t *root, *copy_root;
  const char *copy_path;
  svn_revnum_t mid_rev;
  const svn_fs_id_t *start_node_id, *curr_node_id;
  svn_error_t *err;

  /* Validate the revision range. */
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

  /* Ensure path exists in fs at start revision. */
  SVN_ERR(svn_fs_revision_root(&root, fs, start, pool));
  err = svn_fs_node_id(&start_node_id, root, path, pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_FS_NOT_FOUND)
        {
          /* Path must exist in fs at start rev. */
          *deleted = SVN_INVALID_REVNUM;
          svn_error_clear(err);
          return SVN_NO_ERROR;
        }
      return err;
    }

  /* Ensure path was deleted at or before end revision. */
  SVN_ERR(svn_fs_revision_root(&root, fs, end, pool));
  err = svn_fs_node_id(&curr_node_id, root, path, pool);
  if (err && err->apr_err == SVN_ERR_FS_NOT_FOUND)
    {
      svn_error_clear(err);
    }
  else if (err)
    {
      return err;
    }
  else
    {
      /* path exists in the end node and the end node is equivalent
         or otherwise equivalent to the start node.  This can mean
         a few things:

           1) The end node *is* simply the start node, uncopied
              and unmodified in the start to end range.

           2) The start node was modified, but never copied.

           3) The start node was copied, but this copy occurred at
              start or some rev *previous* to start, this is
              effectively the same situation as 1 if the node was
              never modified or 2 if it was.

         In the first three cases the path was not deleted in
         the specified range and we are done, but in the following
         cases the start node must have been deleted at least once:

           4) The start node was deleted and replaced by a copy of
              itself at some rev between start and end.  This copy
              may itself have been replaced with copies of itself.

           5) The start node was deleted and replaced by a node which
              it does not share any history with.
      */
      SVN_ERR(svn_fs_node_id(&curr_node_id, root, path, pool));
      if (svn_fs_compare_ids(start_node_id, curr_node_id) != -1)
        {
          SVN_ERR(svn_fs_closest_copy(&copy_root, &copy_path, root,
                                      path, pool));
          if (!copy_root ||
              (svn_fs_revision_root_revision(copy_root) <= start))
            {
              /* Case 1,2 or 3, nothing more to do. */
              *deleted = SVN_INVALID_REVNUM;
              return SVN_NO_ERROR;
            }
        }
    }

  /* If we get here we know that path exists in rev start and was deleted
     at least once before rev end.  To find the revision path was first
     deleted we use a binary search.  The rules for the determining if
     the deletion comes before or after a given median revision are
     described by this matrix:

                   |             Most recent copy event that
                   |               caused mid node to exist.
                   |-----------------------------------------------------
     Compare path  |                   |                |               |
     at start and  |   Copied at       |  Copied at     | Never copied  |
     mid nodes.    |   rev > start     |  rev <= start  |               |
                   |                   |                |               |
     -------------------------------------------------------------------|
     Mid node is   |  A) Start node    |                                |
     equivalent to |     replaced with |  E) Mid node == start node,    |
     start node    |     an unmodified |     look HIGHER.               |
                   |     copy of       |                                |
                   |     itself,       |                                |
                   |     look LOWER.   |                                |
     -------------------------------------------------------------------|
     Mid node is   |  B) Start node    |                                |
     otherwise     |     replaced with |  F) Mid node is a modified     |
     related to    |     a modified    |     version of start node,     |
     start node    |     copy of       |     look HIGHER.               |
                   |     itself,       |                                |
                   |     look LOWER.   |                                |
     -------------------------------------------------------------------|
     Mid node is   |                                                    |
     unrelated to  |  C) Start node replaced with unrelated mid node,   |
     start node    |     look LOWER.                                    |
                   |                                                    |
     -------------------------------------------------------------------|
     Path doesn't  |                                                    |
     exist at mid  |  D) Start node deleted before mid node,            |
     node          |     look LOWER                                     |
                   |                                                    |
     --------------------------------------------------------------------
  */

  mid_rev = (start + end) / 2;
  subpool = svn_pool_create(pool);

  while (1)
    {
      svn_pool_clear(subpool);

      /* Get revision root and node id for mid_rev at that revision. */
      SVN_ERR(svn_fs_revision_root(&root, fs, mid_rev, subpool));
      err = svn_fs_node_id(&curr_node_id, root, path, subpool);

      if (err)
        {
          if (err->apr_err == SVN_ERR_FS_NOT_FOUND)
            {
              /* Case D: Look lower in the range. */
              svn_error_clear(err);
              end = mid_rev;
              mid_rev = (start + mid_rev) / 2;
            }
          else
            return err;
        }
      else
        {
          /* Determine the relationship between the start node
             and the current node. */
          int cmp = svn_fs_compare_ids(start_node_id, curr_node_id);
          SVN_ERR(svn_fs_closest_copy(&copy_root, &copy_path, root,
                                      path, subpool));
          if (cmp == -1 ||
              (copy_root &&
               (svn_fs_revision_root_revision(copy_root) > start)))
            {
              /* Cases A, B, C: Look at lower revs. */
              end = mid_rev;
              mid_rev = (start + mid_rev) / 2;
            }
          else if (end - mid_rev == 1)
            {
              /* Found the node path was deleted. */
              *deleted = end;
              break;
            }
          else
            {
              /* Cases E, F: Look at higher revs. */
              start = mid_rev;
              mid_rev = (start + end) / 2;
            }
        }
    }

  svn_pool_destroy(subpool);
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


/* Set *PREV_PATH and *PREV_REV to the path and revision which
   represent the location at which PATH in FS was located immediately
   prior to REVISION iff there was a copy operation (to PATH or one of
   its parent directories) between that previous location and
   PATH@REVISION, and set *APPEARED_REV to the first revision in which
   PATH@REVISION appeared at PATH as a result of that copy operation.

   If there was no such copy operation in that portion
   of PATH's history, set *PREV_PATH to NULL, and set *PREV_REV and
   *APPEARED_REV to SVN_INVALID_REVNUM.  */
static svn_error_t *
prev_location(svn_revnum_t *appeared_rev,
              const char **prev_path,
              svn_revnum_t *prev_rev,
              svn_fs_t *fs,
              svn_revnum_t revision,
              const char *path,
              apr_pool_t *pool)
{
  svn_fs_root_t *root, *copy_root;
  const char *copy_path, *copy_src_path, *remainder = "";
  svn_revnum_t copy_src_rev;

  /* Initialize return variables. */
  *appeared_rev = *prev_rev = SVN_INVALID_REVNUM;
  *prev_path = NULL;

  /* Ask about the most recent copy which affected PATH@REVISION.  If
     there was no such copy, we're done.  */
  SVN_ERR(svn_fs_revision_root(&root, fs, revision, pool));
  SVN_ERR(svn_fs_closest_copy(&copy_root, &copy_path, root, path, pool));
  if (! copy_root)
    return SVN_NO_ERROR;    

  /* Ultimately, it's not the path of the closest copy's source that
     we care about -- it's our own path's location in the copy source
     revision.  So we'll tack the relative path that expresses the
     difference between the copy destination and our path in the copy
     revision onto the copy source path to determine this information.

     In other words, if our path is "/branches/my-branch/foo/bar", and
     we know that the closest relevant copy was a copy of "/trunk" to
     "/branches/my-branch", then that relative path under the copy
     destination is "/foo/bar".  Tacking that onto the copy source
     path tells us that our path was located at "/trunk/foo/bar"
     before the copy.
  */
  SVN_ERR(svn_fs_copied_from(&copy_src_rev, &copy_src_path, 
                             copy_root, copy_path, pool));
  if (! strcmp(copy_path, path) == 0)
    remainder = svn_path_is_child(copy_path, path, pool);
  *prev_path = svn_path_join(copy_src_path, remainder, pool);
  *appeared_rev = svn_fs_revision_root_revision(copy_root);
  *prev_rev = copy_src_rev;
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
      svn_revnum_t appeared_rev, prev_rev;
      const char *prev_path;

      /* Find the target of the innermost copy relevant to path@revision.
         The copy may be of path itself, or of a parent directory. */
      SVN_ERR(prev_location(&appeared_rev, &prev_path, &prev_rev, fs, 
                            revision, path, currpool));
      if (! prev_path)
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
      while ((revision_ptr < revision_ptr_end) 
             && (*revision_ptr >= appeared_rev))
        {
          /* *revision_ptr is allocated out of pool, so we can point
             to in the hash table. */
          apr_hash_set(*locations, revision_ptr, sizeof(*revision_ptr),
                       apr_pstrdup(pool, path));
          revision_ptr++;
        }

      /* Ignore all revs between the copy target rev and the copy
         source rev (non-inclusive). */
      while ((revision_ptr < revision_ptr_end) 
             && (*revision_ptr > prev_rev))
        revision_ptr++;

      /* State update. */
      path = prev_path;
      revision = prev_rev;

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


/* This implements the `svn_repos_history_func_t' interface, and is
   used by svn_repos_node_location_segments() to determine, for a path
   and revision which has no prior affecting copies, the revision in
   which the path was created.  It's baton is a pointer to an
   svn_revnum_t, which is clobbered by each successive REVISION. */
static svn_error_t *
nls_history_func(void *baton,
                 const char *path,
                 svn_revnum_t revision,
                 apr_pool_t *pool)
{
  svn_revnum_t *b = baton;
  *b = revision;
  return SVN_NO_ERROR;
}


/* Transmit SEGMENT through RECEIVER/RECEIVER_BATON iff a portion of
   its revision range fits between END_REV and START_REV, possibly
   cropping the range so that it fits *entirely* in that range. */
static svn_error_t *
maybe_crop_and_send_segment(svn_location_segment_t *segment,
                            svn_revnum_t start_rev,
                            svn_revnum_t end_rev,
                            svn_location_segment_receiver_t receiver,
                            void *receiver_baton,
                            apr_pool_t *pool)
{
  /* We only want to transmit this segment if some portion of it
     is between our END_REV and START_REV. */
  if (! ((segment->range_start > start_rev)
         || (segment->range_end < end_rev)))
    {
      /* Correct our segment range when the range straddles one of
         our requested revision boundaries. */
      if (segment->range_start < end_rev)
        segment->range_start = end_rev;
      if (segment->range_end > start_rev)
        segment->range_end = start_rev;
      SVN_ERR(receiver(segment, receiver_baton, pool));
    }
  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_node_location_segments(svn_repos_t *repos,
                                 const char *path,
                                 svn_revnum_t peg_revision,
                                 svn_revnum_t start_rev,
                                 svn_revnum_t end_rev,
                                 svn_location_segment_receiver_t receiver,
                                 void *receiver_baton,
                                 svn_repos_authz_func_t authz_read_func,
                                 void *authz_read_baton,
                                 apr_pool_t *pool)
{
  svn_fs_t *fs = svn_repos_fs(repos);
  svn_stringbuf_t *current_path;
  svn_revnum_t youngest_rev = SVN_INVALID_REVNUM, current_rev;
  apr_pool_t *subpool;

  /* No PEG_REVISION?  We'll use HEAD. */
  if (! SVN_IS_VALID_REVNUM(peg_revision))
    {
      SVN_ERR(svn_fs_youngest_rev(&youngest_rev, fs, pool));
      peg_revision = youngest_rev;
    }

  /* No START_REV?  We'll use HEAD (which we may have already fetched). */
  if (! SVN_IS_VALID_REVNUM(start_rev))
    {
      if (SVN_IS_VALID_REVNUM(youngest_rev))
        start_rev = youngest_rev;
      else
        SVN_ERR(svn_fs_youngest_rev(&start_rev, fs, pool)); 
    }

  /* No END_REV?  We'll use 0. */
  end_rev = SVN_IS_VALID_REVNUM(end_rev) ? end_rev : 0;

  /* Are the revision properly ordered?  They better be -- the API
     demands it. */
  assert(end_rev <= start_rev);
  assert(start_rev <= peg_revision);

  /* Ensure that PATH is absolute, because our path-math will depend
     on that being the case.  */
  if (*path != '/')
    path = apr_pstrcat(pool, "/", path, NULL);

  /* Auth check. */
  if (authz_read_func)
    {
      svn_fs_root_t *peg_root;
      SVN_ERR(svn_fs_revision_root(&peg_root, fs, peg_revision, pool));
      SVN_ERR(check_readability(peg_root, path,
                                authz_read_func, authz_read_baton, pool));
    }

  /* Okay, let's get searching! */
  subpool = svn_pool_create(pool);
  current_rev = peg_revision;
  current_path = svn_stringbuf_create(path, pool);
  while (current_rev >= end_rev)
    {
      svn_revnum_t appeared_rev, prev_rev;
      const char *cur_path, *prev_path;
      svn_location_segment_t *segment;

      svn_pool_clear(subpool);

      cur_path = apr_pstrmemdup(subpool, current_path->data,
                                current_path->len);
      segment = apr_pcalloc(subpool, sizeof(*segment));
      segment->range_end = current_rev;
      segment->range_start = end_rev;
      segment->path = cur_path + 1;

      SVN_ERR(prev_location(&appeared_rev, &prev_path, &prev_rev, fs, 
                            current_rev, cur_path, subpool));

      /* If there are no previous locations for this thing (meaning,
         it originated at the current path), then we simply need to
         find its revision of origin to populate our final segment.
         Otherwise, the APPEARED_REV is the start of current segment's
         range. */
      if (! prev_path)
        {
          svn_revnum_t first_rev;
          SVN_ERR(svn_repos_history2(fs, cur_path, nls_history_func,
                                     &first_rev, NULL, NULL, 
                                     current_rev, end_rev, TRUE, subpool));
          segment->range_start = first_rev;
          current_rev = SVN_INVALID_REVNUM;
        }
      else
        {
          segment->range_start = appeared_rev;
          svn_stringbuf_set(current_path, prev_path);
          current_rev = prev_rev;
        }
      
      /* Report our segment, providing it passes authz muster. */
      if (authz_read_func)
        {
          svn_boolean_t readable;
          svn_fs_root_t *cur_rev_root;
          
          SVN_ERR(svn_fs_revision_root(&cur_rev_root, fs, 
                                       segment->range_end, subpool));
          SVN_ERR(authz_read_func(&readable, cur_rev_root, segment->path,
                                  authz_read_baton, subpool));
          if (! readable)
            return SVN_NO_ERROR;
        }

      /* Trasmit the segment (if its within the scope of our concern). */
      SVN_ERR(maybe_crop_and_send_segment(segment, start_rev, end_rev, 
                                          receiver, receiver_baton, subpool));

      /* If we've set CURRENT_REV to SVN_INVALID_REVNUM, we're done
         (and didn't ever reach END_REV).  */
      if (! SVN_IS_VALID_REVNUM(current_rev))
        break;

      /* If there's a gap in the history, we need to report as much
         (if the gap is within the scope of our concern). */
      if (segment->range_start - current_rev > 1)
        {
          svn_location_segment_t *gap_segment;
          gap_segment = apr_pcalloc(subpool, sizeof(*gap_segment));
          gap_segment->range_end = segment->range_start - 1;
          gap_segment->range_start = current_rev + 1;
          gap_segment->path = NULL;
          SVN_ERR(maybe_crop_and_send_segment(gap_segment, start_rev, end_rev,
                                              receiver, receiver_baton, 
                                              subpool));
        }
    }
  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}


struct path_revision
{
  svn_revnum_t revnum;
  const char *path;

  /* Merged revision flag.  This is set if the path/revision pair is the
     result of a merge. */
  svn_boolean_t merged_revision;
};

/* Context to be used when sending an individual revision. */
struct send_ctx_t
{
  svn_repos_t *repos;
  svn_file_rev_handler_t handler;
  void *handler_baton;

  apr_pool_t *iter_pool;
  apr_pool_t *last_pool;
  svn_fs_root_t *last_root;
  const char *last_path;
  apr_hash_t *last_props;
};

/* Baton for walking ancestry. */
struct ancestry_walker_baton
{
  apr_array_header_t *path_revisions;
  apr_pool_t *mainpool;

  /* For get_file_ancestry(). */
  struct send_ctx_t *send_ctx;
};

/* This implements svn_repos__ancestry_callbacks_t.found_ancestor() */
static svn_error_t *
revs_found_ancestor(void *baton,
                    const char *path,
                    svn_revnum_t rev,
                    apr_pool_t *pool)
{
  struct ancestry_walker_baton *awb = baton;
  struct path_revision *path_rev = apr_palloc(awb->mainpool, sizeof(*path_rev));

  path_rev->path = apr_pstrdup(awb->mainpool, path);
  path_rev->revnum = rev;

  APR_ARRAY_PUSH(awb->path_revisions, struct path_revision *) = path_rev;

  return SVN_NO_ERROR;
}

static svn_error_t *
send_revision(const char *path,
              svn_revnum_t rev,
              struct send_ctx_t *send_ctx,
              apr_pool_t *pool)
{
  apr_hash_t *rev_props;
  apr_hash_t *props;
  apr_array_header_t *prop_diffs;
  svn_fs_root_t *root;
  svn_txdelta_stream_t *delta_stream;
  svn_txdelta_window_handler_t delta_handler = NULL;
  void *delta_baton = NULL;
  apr_pool_t *tmp_pool;  /* For swapping */
  svn_boolean_t contents_changed;

  svn_pool_clear(send_ctx->iter_pool);

  /* Get the revision properties. */
  SVN_ERR(svn_fs_revision_proplist(&rev_props, send_ctx->repos->fs, rev,
                                   send_ctx->iter_pool));

  /* Open the revision root. */
  SVN_ERR(svn_fs_revision_root(&root, send_ctx->repos->fs, rev,
                               send_ctx->iter_pool));

  /* Get the file's properties for this revision and compute the diffs. */
  SVN_ERR(svn_fs_node_proplist(&props, root, path, send_ctx->iter_pool));
  SVN_ERR(svn_prop_diffs(&prop_diffs, props, send_ctx->last_props, pool));

  /* Check if the contents changed. */
  /* Special case: In the first revision, we always provide a delta. */
  if (send_ctx->last_root)
    SVN_ERR(svn_fs_contents_changed(&contents_changed, send_ctx->last_root,
                                    send_ctx->last_path, root, path,
                                    send_ctx->iter_pool));
  else
    contents_changed = TRUE;

  /* We have all we need, give to the handler. */
  SVN_ERR(send_ctx->handler(send_ctx->handler_baton, path, rev, rev_props,
                            FALSE, contents_changed ? &delta_handler : NULL,
                            contents_changed ? &delta_baton : NULL,
                            prop_diffs, send_ctx->iter_pool));

  /* Compute and send delta if client asked for it.
     Note that this was initialized to NULL, so if !contents_changed,
     no deltas will be computed. */
  if (delta_handler)
    {
      /* Get the content delta. */
      SVN_ERR(svn_fs_get_file_delta_stream(&delta_stream,
                                           send_ctx->last_root,
                                           send_ctx->last_path,
                                           root, path, send_ctx->iter_pool));
      /* And send. */
      SVN_ERR(svn_txdelta_send_txstream(delta_stream,
                                        delta_handler, delta_baton,
                                        send_ctx->iter_pool));
    }

  /* Remember root, path and props for next iteration. */
  send_ctx->last_root = root;
  send_ctx->last_path = apr_pstrdup(send_ctx->iter_pool, path);
  send_ctx->last_props = props;

  /* Swap the pools. */
  tmp_pool = send_ctx->iter_pool;
  send_ctx->iter_pool = send_ctx->last_pool;
  send_ctx->last_pool = tmp_pool;

  return SVN_NO_ERROR;
}

static svn_error_t *
send_path_revision_list(apr_array_header_t *path_revisions,
                        svn_repos_t *repos,
                        svn_file_rev_handler_t handler,
                        void *handler_baton,
                        apr_pool_t *pool)
{
  apr_pool_t *iterpool;
  struct send_ctx_t send_ctx;
  int i;

  /* Setup the send context. */
  send_ctx.repos = repos;
  send_ctx.handler = handler;
  send_ctx.handler_baton = handler_baton;
  /* We switch betwwen two pools while looping, since we need information from
     the last iteration to be available. */
  send_ctx.iter_pool = svn_pool_create(pool);
  send_ctx.last_pool = svn_pool_create(pool);
  /* We want the first txdelta to be against the empty file. */
  send_ctx.last_root = NULL;
  send_ctx.last_path = NULL;
  /* Create an empty hash table for the first property diff. */
  send_ctx.last_props = apr_hash_make(send_ctx.last_pool);

  /* Walk through the revisions in chronological order. */
  iterpool = svn_pool_create(pool);
  for (i = path_revisions->nelts; i > 0; --i)
    {
      struct path_revision *path_rev = APR_ARRAY_IDX(path_revisions, i - 1,
                                                     struct path_revision *);
      svn_pool_clear(iterpool);
      SVN_ERR(send_revision(path_rev->path, path_rev->revnum, &send_ctx,
                            iterpool));
    }

  svn_pool_destroy(iterpool);
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
  svn_repos__ancestry_callbacks_t walk_callbacks =
    { revs_found_ancestor, NULL, NULL, NULL };
  struct ancestry_walker_baton awb;
  svn_fs_root_t *root;
  svn_node_kind_t kind;
  svn_file_rev_handler_t handler2;
  void *handler2_baton;

  /* The path had better be a file in this revision. This avoids calling
     the callback before reporting an uglier error below. */
  SVN_ERR(svn_fs_revision_root(&root, repos->fs, end, pool));
  SVN_ERR(svn_fs_check_path(&kind, root, path, pool));
  if (kind != svn_node_file)
    return svn_error_createf
      (SVN_ERR_FS_NOT_FILE, NULL, _("'%s' is not a file in %ld"), path, end);

  /* Setup the ancesty walker baton. */
  awb.path_revisions = apr_array_make(pool, 0, sizeof(struct path_revision *));
  awb.mainpool = pool;

  /* Get the revisions we are interested in. */
  SVN_ERR(svn_repos__walk_ancestry(path, repos->fs, start, end,
                                   FALSE, FALSE, &walk_callbacks, &awb,
                                   authz_read_func, authz_read_baton, pool));

  /* We must have at least one revision to get. */
  assert(awb.path_revisions->nelts > 0);

  svn_compat_wrap_file_rev_handler(&handler2, &handler2_baton, handler,
                                   handler_baton, pool);

  /* Send the revision list to the client. */
  return send_path_revision_list(awb.path_revisions, repos,
                                 handler2, handler2_baton, pool);
}

static int
compare_path_revision_revs(const void *a, const void *b)
{
  const struct path_revision *a_path_rev = *(const struct path_revision **)a;
  const struct path_revision *b_path_rev = *(const struct path_revision **)b;

  if (a_path_rev->revnum == b_path_rev->revnum)
    {
      int strcmp_result = strcmp(a_path_rev->path, b_path_rev->path);

      if (strcmp_result == 0)
        {
          if (a_path_rev->merged_revision == b_path_rev->merged_revision)
            return 0;

          return a_path_rev->merged_revision == TRUE ? 1 : -1;
        }

      return strcmp_result;
    }

  return a_path_rev->revnum < b_path_rev->revnum ? 1 : -1;
}

static svn_error_t *
sort_and_scrub_revisions(apr_array_header_t **path_revisions,
                         apr_pool_t *pool)
{
  int i;
  struct path_revision previous_path_rev = { 0, NULL, FALSE };
  apr_array_header_t *out_path_revisions = apr_array_make(pool, 0,
                                            sizeof(struct path_revision *));

  /* Sort the path_revision pairs by revnum in descending order, then path. */
  qsort((*path_revisions)->elts, (*path_revisions)->nelts,
        (*path_revisions)->elt_size, compare_path_revision_revs);

  /* Filter out duplicat path/revision pairs.  Because we ensured that pairs
     without the merged_revision flag set are ordered after pair with it set,
     the following scrubbing process will prefer path/revision pairs from the
     mainline of history, and not the result of a merge. */
  for (i = 0; i < (*path_revisions)->nelts; i++)
    {
      struct path_revision *path_rev = APR_ARRAY_IDX(*path_revisions, i,
                                                     struct path_revision *);

      if ( (previous_path_rev.revnum != path_rev->revnum)
            || (strcmp(previous_path_rev.path, path_rev->path) != 0) )
        {
          APR_ARRAY_PUSH(out_path_revisions, struct path_revision *) = path_rev;
        }

      previous_path_rev = *path_rev;
    }

  *path_revisions = out_path_revisions;

  return SVN_NO_ERROR;
}

/* This implements svn_repos__ancestry_callbacks_t.found_ancestor() */
static svn_error_t *
ancestry_found_ancestor(void *baton,
                        const char *path,
                        svn_revnum_t rev,
                        apr_pool_t *pool)
{
  struct ancestry_walker_baton *awb = baton;

  return send_revision(path, rev, awb->send_ctx, pool);
}

svn_error_t *
svn_repos_get_file_ancestry(svn_repos_t *repos,
                            const char *path,
                            svn_revnum_t start,
                            svn_revnum_t end,
                            svn_boolean_t include_merged_revisions,
                            svn_repos_authz_func_t authz_read_func,
                            void *authz_read_baton,
                            svn_file_rev_handler_t handler,
                            void *handler_baton,
                            apr_pool_t *pool)
{
  svn_repos__ancestry_callbacks_t walk_callbacks =
    { ancestry_found_ancestor, NULL, NULL, NULL };
  svn_fs_root_t *root;
  svn_node_kind_t kind;
  struct ancestry_walker_baton awb;
  struct send_ctx_t send_ctx;

  /* Check to make sure we are operating on a file. */
  SVN_ERR(svn_fs_revision_root(&root, repos->fs, end, pool));
  SVN_ERR(svn_fs_check_path(&kind, root, path, pool));
  if (kind != svn_node_file)
    return svn_error_createf(SVN_ERR_FS_NOT_FILE, NULL,
      _("'%s' is not a file in revision %ld"), path, end);

  /* Setup the ancesty walker baton. */
  send_ctx.repos = repos;
  send_ctx.handler = handler;
  send_ctx.handler_baton = handler_baton;
  /* We switch betwwen two pools while looping, since we need information from
     the last iteration to be available. */
  send_ctx.last_pool = svn_pool_create(pool);
  send_ctx.iter_pool = svn_pool_create(pool);
  /* We want the first txdelta to be against the empty file. */
  send_ctx.last_root = NULL;
  send_ctx.last_path = NULL;
  send_ctx.last_props = apr_hash_make(send_ctx.last_pool);

  awb.path_revisions = apr_array_make(pool, 0, sizeof(struct path_revision *));
  awb.mainpool = pool;
  awb.send_ctx = &send_ctx;

  /* Walk the node ancestry. */
  SVN_ERR(svn_repos__walk_ancestry(path, repos->fs, start, end,
                                   include_merged_revisions, FALSE,
                                   &walk_callbacks, &awb,
                                   authz_read_func, authz_read_baton, pool));

  svn_pool_destroy(send_ctx.iter_pool);
  svn_pool_destroy(send_ctx.last_pool);

  return SVN_NO_ERROR;
}
