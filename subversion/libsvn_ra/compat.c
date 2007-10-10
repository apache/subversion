/*
 * compat.c:  compatibility compliance logic
 *
 * ====================================================================
 * Copyright (c) 2004-2007 CollabNet.  All rights reserved.
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

/* ==================================================================== */

/*** Includes. ***/
#include <apr_pools.h>
#include <assert.h>

#include "svn_error.h"
#include "svn_pools.h"
#include "svn_sorts.h"
#include "svn_path.h"
#include "svn_ra.h"
#include "ra_loader.h"
#include "svn_private_config.h"



/* ### This is to support 1.0 servers. */
struct log_receiver_baton
{
  /* The kind of the path we're tracing. */
  svn_node_kind_t kind;

  /* The path at which we are trying to find our versioned resource in
     the log output. */
  const char *last_path;

  /* Input revisions and output hash; the whole point of this little game. */
  svn_revnum_t peg_revision;
  apr_array_header_t *location_revisions;
  const char *peg_path;
  apr_hash_t *locations;

  /* A pool from which to allocate stuff stored in this baton. */
  apr_pool_t *pool;
};


/* Given the CHANGED_PATHS and REVISION from an instance of a
   svn_log_message_receiver_t function, determine at which location
   PATH may be expected in the next log message, and set *PREV_PATH_P
   to that value.  KIND is the node kind of PATH.  Set *ACTION_P to a
   character describing the change that caused this revision (as
   listed in svn_log_changed_path_t) and set *COPYFROM_REV_P to the
   revision PATH was copied from, or SVN_INVALID_REVNUM if it was not
   copied.  ACTION_P and COPYFROM_REV_P may be NULL, in which case
   they are not used.  Perform all allocations in POOL.

   This is useful for tracking the various changes in location a
   particular resource has undergone when performing an RA->get_logs()
   operation on that resource.  

   ### NOTE: This is a perfect duplicate of
   ### libsvn_client/blame.c:prev_log_path(), which should someday go
   ### away when the blame compat code is moved into this file, too.
*/
static svn_error_t *
prev_log_path(const char **prev_path_p,
              char *action_p,
              svn_revnum_t *copyfrom_rev_p,
              apr_hash_t *changed_paths,
              const char *path,
              svn_node_kind_t kind,
              svn_revnum_t revision,
              apr_pool_t *pool)
{
  svn_log_changed_path_t *change;
  const char *prev_path = NULL;

  /* It's impossible to find the predecessor path of a NULL path. */
  assert(path);

  /* Initialize our return values for the action and copyfrom_rev in
     case we have an unhandled case later on. */
  if (action_p)
    *action_p = 'M';
  if (copyfrom_rev_p)
    *copyfrom_rev_p = SVN_INVALID_REVNUM;

  /* See if PATH was explicitly changed in this revision. */
  change = apr_hash_get(changed_paths, path, APR_HASH_KEY_STRING);
  if (change)
    {
      /* If PATH was not newly added in this revision, then it may or may
         not have also been part of a moved subtree.  In this case, set a
         default previous path, but still look through the parents of this
         path for a possible copy event. */
      if (change->action != 'A' && change->action != 'R')
        {
          prev_path = path;
        }
      else
        {
          /* PATH is new in this revision.  This means it cannot have been
             part of a copied subtree. */
          if (change->copyfrom_path)
            prev_path = apr_pstrdup(pool, change->copyfrom_path);
          else
            prev_path = NULL;

          *prev_path_p = prev_path;
          if (action_p)
            *action_p = change->action;
          if (copyfrom_rev_p)
            *copyfrom_rev_p = change->copyfrom_rev;
          return SVN_NO_ERROR;
        }
    }

  if (apr_hash_count(changed_paths))
    {
      /* The path was not explicitly changed in this revision.  The
         fact that we're hearing about this revision implies, then,
         that the path was a child of some copied directory.  We need
         to find that directory, and effectively "re-base" our path on
         that directory's copyfrom_path. */
      int i;
      apr_array_header_t *paths;

      /* Build a sorted list of the changed paths. */
      paths = svn_sort__hash(changed_paths,
                             svn_sort_compare_items_as_paths, pool);

      /* Now, walk the list of paths backwards, looking a parent of
         our path that has copyfrom information. */
      for (i = paths->nelts; i > 0; i--)
        {
          svn_sort__item_t item = APR_ARRAY_IDX(paths,
                                                i - 1, svn_sort__item_t);
          const char *ch_path = item.key;
          int len = strlen(ch_path);

          /* See if our path is the child of this change path.  If
             not, keep looking.  */
          if (! ((strncmp(ch_path, path, len) == 0) && (path[len] == '/')))
            continue;

          /* Okay, our path *is* a child of this change path.  If
             this change was copied, we just need to apply the
             portion of our path that is relative to this change's
             path, to the change's copyfrom path.  Otherwise, this
             change isn't really interesting to us, and our search
             continues. */
          change = apr_hash_get(changed_paths, ch_path, len);
          if (change->copyfrom_path)
            {
              if (action_p)
                *action_p = change->action;
              if (copyfrom_rev_p)
                *copyfrom_rev_p = change->copyfrom_rev;
              prev_path = svn_path_join(change->copyfrom_path,
                                        path + len + 1, pool);
              break;
            }
        }
    }

  /* If we didn't find what we expected to find, return an error.
     (Because directories bubble-up, we get a bunch of logs we might
     not want.  Be forgiving in that case.)  */
  if (! prev_path)
    {
      if (kind == svn_node_dir)
        prev_path = apr_pstrdup(pool, path);
      else
        return svn_error_createf(SVN_ERR_CLIENT_UNRELATED_RESOURCES, NULL,
                                 _("Missing changed-path information for "
                                   "'%s' in revision %ld"),
                                 svn_path_local_style(path, pool), revision);
    }

  *prev_path_p = prev_path;
  return SVN_NO_ERROR;
}


/* Implements svn_log_entry_receiver_t; helper for slow_get_locations.
   As input, takes log_receiver_baton (defined above) and attempts to
   "fill in" locations in the baton over the course of many
   iterations. */
static svn_error_t *
log_receiver(void *baton,
             svn_log_entry_t *log_entry,
             apr_pool_t *pool)
{
  struct log_receiver_baton *lrb = baton;
  apr_pool_t *hash_pool = apr_hash_pool_get(lrb->locations);
  const char *current_path = lrb->last_path;
  const char *prev_path;

  /* No paths were changed in this revision.  Nothing to do. */
  if (! log_entry->changed_paths)
    return SVN_NO_ERROR;

  /* If we've run off the end of the path's history, there's nothing
     to do.  (This should never happen with a properly functioning
     server, since we'd get no more log messages after the one where
     path was created.  But a malfunctioning server shouldn't cause us
     to trigger an assertion failure.) */
  if (! current_path)
    return SVN_NO_ERROR;

  /* If we haven't found our peg path yet, and we are now looking at a
     revision equal to or older than the peg revision, then our
     "current" path is our peg path. */
  if ((! lrb->peg_path) && (log_entry->revision <= lrb->peg_revision))
    lrb->peg_path = apr_pstrdup(lrb->pool, current_path);

  /* Determine the paths for any of the revisions for which we haven't
     gotten paths already. */
  while (lrb->location_revisions->nelts)
    {
      svn_revnum_t next = APR_ARRAY_IDX(lrb->location_revisions,
                                        lrb->location_revisions->nelts - 1,
                                        svn_revnum_t);
      if (log_entry->revision <= next)
        {
          apr_hash_set(lrb->locations,
                       apr_pmemdup(hash_pool, &next, sizeof(next)),
                       sizeof(next), 
                       apr_pstrdup(hash_pool, current_path));
          apr_array_pop(lrb->location_revisions);
        }
      else
        break;
    }

  /* Figure out at which repository path our object of interest lived
     in the previous revision. */
  SVN_ERR(prev_log_path(&prev_path, NULL, NULL, log_entry->changed_paths,
                        current_path, lrb->kind, log_entry->revision, pool));

  /* Squirrel away our "next place to look" path (suffer the strcmp
     hit to save on allocations). */
  if (! prev_path)
    lrb->last_path = NULL;
  else if (strcmp(prev_path, current_path) != 0)
    lrb->last_path = apr_pstrdup(lrb->pool, prev_path);

  return SVN_NO_ERROR;
}


/* This is just like svn_sort_compare_revisions, save that it sorts
   the revisions in *ascending* order. */
static int
compare_revisions(const void *a, const void *b)
{
  svn_revnum_t a_rev = *(const svn_revnum_t *)a;
  svn_revnum_t b_rev = *(const svn_revnum_t *)b;
  if (a_rev == b_rev)
    return 0;
  return a_rev < b_rev ? -1 : 1;
}


svn_error_t *
svn_ra__locations_from_log(svn_ra_session_t *session,
                           apr_hash_t **locations_p,
                           const char *path,
                           svn_revnum_t peg_revision,
                           apr_array_header_t *location_revisions,
                           apr_pool_t *pool)
{
  apr_hash_t *locations = apr_hash_make(pool);
  struct log_receiver_baton lrb = { 0 };
  apr_array_header_t *targets;
  svn_revnum_t youngest_requested, oldest_requested, youngest, oldest;
  svn_node_kind_t kind;
  const char *root_url, *url, *rel_path;

  /* Fetch the repository root URL and relative path. */
  SVN_ERR(svn_ra_get_repos_root(session, &root_url, pool));
  SVN_ERR(svn_ra_get_session_url(session, &url, pool));
  url = svn_path_join(url, path, pool);
  rel_path = svn_path_uri_decode(url + strlen(root_url), pool);

  /* Sanity check: verify that the peg-object exists in repos. */
  SVN_ERR(svn_ra_check_path(session, path, peg_revision, &kind, pool));
  if (kind == svn_node_none)
    return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                             _("Path '%s' doesn't exist in revision %ld"),
                             rel_path, peg_revision);

  /* Easy out: no location revisions. */
  if (! location_revisions->nelts)
    {
      *locations_p = locations;
      return SVN_NO_ERROR;
    }

  /* Figure out the youngest and oldest revs (amongst the set of
     requested revisions + the peg revision) so we can avoid
     unnecessary log parsing. */
  qsort(location_revisions->elts, location_revisions->nelts, 
        location_revisions->elt_size, compare_revisions);
  oldest_requested = APR_ARRAY_IDX(location_revisions, 0, svn_revnum_t);
  youngest_requested = APR_ARRAY_IDX(location_revisions, 
                                     location_revisions->nelts - 1,
                                     svn_revnum_t);
  youngest = peg_revision;
  youngest = (oldest_requested > youngest) ? oldest_requested : youngest;
  youngest = (youngest_requested > youngest) ? youngest_requested : youngest;
  oldest = peg_revision;
  oldest = (oldest_requested < oldest) ? oldest_requested : oldest;
  oldest = (youngest_requested < oldest) ? youngest_requested : oldest;



  /* Populate most of our log receiver baton structure. */
  lrb.kind = kind;
  lrb.last_path = rel_path;
  lrb.location_revisions = apr_array_copy(pool, location_revisions);
  lrb.peg_revision = peg_revision;
  lrb.peg_path = NULL;
  lrb.locations = locations;
  lrb.pool = pool;

  /* Let the RA layer drive our log information handler, which will do
     the work of finding the actual locations for our resource.
     Notice that we always run on the youngest rev of the 3 inputs. */
  targets = apr_array_make(pool, 1, sizeof(const char *));
  APR_ARRAY_PUSH(targets, const char *) = path;
  SVN_ERR(svn_ra_get_log2(session, targets, youngest, oldest, 0,
                          TRUE, FALSE, FALSE, 
                          apr_array_make(pool, 0, sizeof(const char *)),
                          log_receiver, &lrb, pool));

  /* If the received log information did not cover any of the
     requested revisions, use the last known path.  (This normally
     just means that ABS_PATH was not modified between the requested
     revision and OLDEST.  If the file was created at some point after
     OLDEST, then lrb.last_path should be NULL.) */
  if (! lrb.peg_path)
    lrb.peg_path = lrb.last_path;
  if (lrb.last_path)
    {
      int i;
      for (i = 0; i < location_revisions->nelts; i++)
        {
          svn_revnum_t rev = APR_ARRAY_IDX(location_revisions, i, 
                                           svn_revnum_t);
          if (! apr_hash_get(locations, &rev, sizeof(rev)))
            apr_hash_set(locations, apr_pmemdup(pool, &rev, sizeof(rev)),
                         sizeof(rev), apr_pstrdup(pool, lrb.last_path));
        }
    }

  /* Check that we got the peg path. */
  if (! lrb.peg_path)
    return svn_error_createf
      (APR_EGENERAL, NULL,
       _("Unable to find repository location for '%s' in revision %ld"),
       rel_path, peg_revision);

  /* Sanity check: make sure that our calculated peg path is the same
     as what we expected it to be. */
  if (strcmp(rel_path, lrb.peg_path) != 0)
    return svn_error_createf
      (SVN_ERR_CLIENT_UNRELATED_RESOURCES, NULL,
       _("'%s' in revision %ld is an unrelated object"),
       rel_path, youngest);

  *locations_p = locations;
  return SVN_NO_ERROR;
}
