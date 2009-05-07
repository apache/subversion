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

#include <apr_pools.h>

#include "svn_error.h"
#include "svn_pools.h"
#include "svn_sorts.h"
#include "svn_path.h"
#include "svn_ra.h"
#include "svn_io.h"
#include "svn_compat.h"
#include "svn_props.h"

#include "ra_loader.h"
#include "svn_private_config.h"



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
  SVN_ERR_ASSERT(path);

  /* Initialize our return values for the action and copyfrom_rev in
     case we have an unhandled case later on. */
  if (action_p)
    *action_p = 'M';
  if (copyfrom_rev_p)
    *copyfrom_rev_p = SVN_INVALID_REVNUM;

  if (changed_paths)
    {
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



/*** Fallback implementation of svn_ra_get_locations(). ***/


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
  if (! log_entry->changed_paths2)
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
  SVN_ERR(prev_log_path(&prev_path, NULL, NULL, log_entry->changed_paths2,
                        current_path, lrb->kind, log_entry->revision, pool));

  /* Squirrel away our "next place to look" path (suffer the strcmp
     hit to save on allocations). */
  if (! prev_path)
    lrb->last_path = NULL;
  else if (strcmp(prev_path, current_path) != 0)
    lrb->last_path = apr_pstrdup(lrb->pool, prev_path);

  return SVN_NO_ERROR;
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
  SVN_ERR(svn_ra_get_repos_root2(session, &root_url, pool));
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




/*** Fallback implementation of svn_ra_get_location_segments(). ***/

struct gls_log_receiver_baton {
  /* The kind of the path we're tracing. */
  svn_node_kind_t kind;

  /* Are we finished (and just listening to log entries because our
     caller won't shut up?). */
  svn_boolean_t done;

  /* The path at which we are trying to find our versioned resource in
     the log output. */
  const char *last_path;

  /* Input data. */
  svn_revnum_t start_rev;

  /* Output intermediate state and callback/baton. */
  svn_revnum_t range_end;
  svn_location_segment_receiver_t receiver;
  void *receiver_baton;

  /* A pool from which to allocate stuff stored in this baton. */
  apr_pool_t *pool;
};

/* Build a node location segment object from PATH, RANGE_START, and
   RANGE_END, and pass it off to RECEIVER/RECEIVER_BATON. */
static svn_error_t *
maybe_crop_and_send_segment(const char *path,
                            svn_revnum_t start_rev,
                            svn_revnum_t range_start,
                            svn_revnum_t range_end,
                            svn_location_segment_receiver_t receiver,
                            void *receiver_baton,
                            apr_pool_t *pool)
{
  svn_location_segment_t *segment = apr_pcalloc(pool, sizeof(*segment));
  segment->path = path ? ((*path == '/') ? path + 1 : path) : NULL;
  segment->range_start = range_start;
  segment->range_end = range_end;
  if (segment->range_start <= start_rev)
    {
      if (segment->range_end > start_rev)
        segment->range_end = start_rev;
      return receiver(segment, receiver_baton, pool);
    }
  return SVN_NO_ERROR;
}

static svn_error_t *
gls_log_receiver(void *baton,
                 svn_log_entry_t *log_entry,
                 apr_pool_t *pool)
{
  struct gls_log_receiver_baton *lrb = baton;
  const char *current_path = lrb->last_path;
  const char *prev_path;
  svn_revnum_t copyfrom_rev;

  /* If we're done, ignore this invocation. */
  if (lrb->done)
    return SVN_NO_ERROR;

  /* Figure out at which repository path our object of interest lived
     in the previous revision, and if its current location is the
     result of copy since then. */
  SVN_ERR(prev_log_path(&prev_path, NULL, &copyfrom_rev,
                        log_entry->changed_paths2, current_path,
                        lrb->kind, log_entry->revision, pool));

  /* If we've run off the end of the path's history, we need to report
     our final segment (and then, we're done). */
  if (! prev_path)
    {
      lrb->done = TRUE;
      return maybe_crop_and_send_segment(current_path, lrb->start_rev,
                                         log_entry->revision, lrb->range_end,
                                         lrb->receiver, lrb->receiver_baton,
                                         pool);
    }

  /* If there was a copy operation of interest... */
  if (SVN_IS_VALID_REVNUM(copyfrom_rev))
    {
      /* ...then report the segment between this revision and the
         last-reported revision. */
      SVN_ERR(maybe_crop_and_send_segment(current_path, lrb->start_rev,
                                          log_entry->revision, lrb->range_end,
                                          lrb->receiver, lrb->receiver_baton,
                                          pool));
      lrb->range_end = log_entry->revision - 1;

      /* And if there was a revision gap, we need to report that, too. */
      if (log_entry->revision - copyfrom_rev > 1)
        {
          SVN_ERR(maybe_crop_and_send_segment(NULL, lrb->start_rev,
                                              copyfrom_rev + 1, lrb->range_end,
                                              lrb->receiver,
                                              lrb->receiver_baton, pool));
          lrb->range_end = copyfrom_rev;
        }

      /* Update our state variables. */
      lrb->last_path = apr_pstrdup(lrb->pool, prev_path);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_ra__location_segments_from_log(svn_ra_session_t *session,
                                   const char *path,
                                   svn_revnum_t peg_revision,
                                   svn_revnum_t start_rev,
                                   svn_revnum_t end_rev,
                                   svn_location_segment_receiver_t receiver,
                                   void *receiver_baton,
                                   apr_pool_t *pool)
{
  struct gls_log_receiver_baton lrb = { 0 };
  apr_array_header_t *targets;
  svn_node_kind_t kind;
  svn_revnum_t youngest_rev = SVN_INVALID_REVNUM;
  const char *root_url, *url, *rel_path;

  /* Fetch the repository root URL and relative path. */
  SVN_ERR(svn_ra_get_repos_root2(session, &root_url, pool));
  SVN_ERR(svn_ra_get_session_url(session, &url, pool));
  url = svn_path_join(url, path, pool);
  rel_path = svn_path_uri_decode(url + strlen(root_url), pool);

  /* If PEG_REVISION is invalid, it means HEAD.  If START_REV is
     invalid, it means HEAD.  If END_REV is SVN_INVALID_REVNUM, we'll
     use 0. */
  if (! SVN_IS_VALID_REVNUM(peg_revision))
    {
      SVN_ERR(svn_ra_get_latest_revnum(session, &youngest_rev, pool));
      peg_revision = youngest_rev;
    }
  if (! SVN_IS_VALID_REVNUM(start_rev))
    {
      if (SVN_IS_VALID_REVNUM(youngest_rev))
        start_rev = youngest_rev;
      else
        SVN_ERR(svn_ra_get_latest_revnum(session, &start_rev, pool));
    }
  if (! SVN_IS_VALID_REVNUM(end_rev))
    {
      end_rev = 0;
    }

  /* The API demands a certain ordering of our revision inputs. Enforce it. */
  SVN_ERR_ASSERT((peg_revision >= start_rev) && (start_rev >= end_rev));

  /* Sanity check: verify that the peg-object exists in repos. */
  SVN_ERR(svn_ra_check_path(session, path, peg_revision, &kind, pool));
  if (kind == svn_node_none)
    return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                             _("Path '%s' doesn't exist in revision %ld"),
                             rel_path, start_rev);

  /* Populate most of our log receiver baton structure. */
  lrb.kind = kind;
  lrb.last_path = rel_path;
  lrb.done = FALSE;
  lrb.start_rev = start_rev;
  lrb.range_end = start_rev;
  lrb.receiver = receiver;
  lrb.receiver_baton = receiver_baton;
  lrb.pool = pool;

  /* Let the RA layer drive our log information handler, which will do
     the work of finding the actual locations for our resource.
     Notice that we always run on the youngest rev of the 3 inputs. */
  targets = apr_array_make(pool, 1, sizeof(const char *));
  APR_ARRAY_PUSH(targets, const char *) = path;
  SVN_ERR(svn_ra_get_log2(session, targets, peg_revision, end_rev, 0,
                          TRUE, FALSE, FALSE,
                          apr_array_make(pool, 0, sizeof(const char *)),
                          gls_log_receiver, &lrb, pool));

  /* If we didn't finish, we need to do so with a final segment send. */
  if (! lrb.done)
    SVN_ERR(maybe_crop_and_send_segment(lrb.last_path, start_rev,
                                        end_rev, lrb.range_end,
                                        receiver, receiver_baton, pool));

  return SVN_NO_ERROR;
}



/*** Fallback implementation of svn_ra_get_file_revs(). ***/

/* The metadata associated with a particular revision. */
struct rev
{
  svn_revnum_t revision; /* the revision number */
  const char *path;      /* the absolute repository path */
  apr_hash_t *props;     /* the revprops for this revision */
  struct rev *next;      /* the next revision */
};

/* File revs log message baton. */
struct fr_log_message_baton {
  const char *path;        /* The path to be processed */
  struct rev *eldest;      /* The eldest revision processed */
  char action;             /* The action associated with the eldest */
  svn_revnum_t copyrev;    /* The revision the eldest was copied from */
  apr_pool_t *pool;
};

/* Callback for log messages: implements svn_log_entry_receiver_t and
   accumulates revision metadata into a chronologically ordered list stored in
   the baton. */
static svn_error_t *
fr_log_message_receiver(void *baton,
                        svn_log_entry_t *log_entry,
                        apr_pool_t *pool)
{
  struct fr_log_message_baton *lmb = baton;
  struct rev *rev;
  apr_hash_index_t *hi;

  rev = apr_palloc(lmb->pool, sizeof(*rev));
  rev->revision = log_entry->revision;
  rev->path = lmb->path;
  rev->next = lmb->eldest;
  lmb->eldest = rev;

  /* Duplicate log_entry revprops into rev->props */
  rev->props = apr_hash_make(lmb->pool);
  for (hi = apr_hash_first(pool, log_entry->revprops); hi;
       hi = apr_hash_next(hi))
    {
      void *val;
      const void *key;

      apr_hash_this(hi, &key, NULL, &val);
      apr_hash_set(rev->props, apr_pstrdup(lmb->pool, key), APR_HASH_KEY_STRING,
                   svn_string_dup(val, lmb->pool));
    }

  return prev_log_path(&lmb->path, &lmb->action,
                       &lmb->copyrev, log_entry->changed_paths2,
                       lmb->path, svn_node_file, log_entry->revision,
                       lmb->pool);
}

svn_error_t *
svn_ra__file_revs_from_log(svn_ra_session_t *ra_session,
                           const char *path,
                           svn_revnum_t start,
                           svn_revnum_t end,
                           svn_file_rev_handler_t handler,
                           void *handler_baton,
                           apr_pool_t *pool)
{
  svn_node_kind_t kind;
  const char *repos_url;
  const char *session_url;
  const char *tmp;
  char *repos_abs_path;
  apr_array_header_t *condensed_targets;
  struct fr_log_message_baton lmb;
  struct rev *rev;
  apr_hash_t *last_props;
  const char *last_path;
  svn_stream_t *last_stream;
  apr_pool_t *currpool, *lastpool;

  SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_url, pool));
  SVN_ERR(svn_ra_get_session_url(ra_session, &session_url, pool));

  /* Create the initial path, using the repos_url and session_url */
  tmp = svn_path_is_child(repos_url, session_url, pool);
  repos_abs_path = apr_palloc(pool, strlen(tmp) + 1);
  repos_abs_path[0] = '/';
  memcpy(repos_abs_path + 1, tmp, strlen(tmp));

  /* Check to make sure we're dealing with a file. */
  SVN_ERR(svn_ra_check_path(ra_session, "", end, &kind, pool));

  if (kind == svn_node_dir)
    return svn_error_createf(SVN_ERR_FS_NOT_FILE, NULL,
                             _("'%s' is not a file"), repos_abs_path);

  condensed_targets = apr_array_make(pool, 1, sizeof(const char *));
  APR_ARRAY_PUSH(condensed_targets, const char *) = "";

  lmb.path = svn_path_uri_decode(repos_abs_path, pool);
  lmb.eldest = NULL;
  lmb.pool = pool;

  /* Accumulate revision metadata by walking the revisions
     backwards; this allows us to follow moves/copies
     correctly. */
  SVN_ERR(svn_ra_get_log2(ra_session,
                          condensed_targets,
                          end, start, 0, /* no limit */
                          TRUE, FALSE, FALSE,
                          NULL, fr_log_message_receiver, &lmb,
                          pool));

  /* Reparent the session while we go back through the history. */
  SVN_ERR(svn_ra_reparent(ra_session, repos_url, pool));

  currpool = svn_pool_create(pool);
  lastpool = svn_pool_create(pool);

  /* We want the first txdelta to be against the empty file. */
  last_props = apr_hash_make(lastpool);
  last_path = NULL;
  last_stream = svn_stream_empty(lastpool);

  /* Walk the revision list in chronological order, downloading each fulltext,
     diffing it with its predecessor, and calling the file_revs handler for
     each one.  Use two iteration pools rather than one, because the diff
     routines need to look at a sliding window of revisions.  Two pools gives
     us a ring buffer of sorts. */
  for (rev = lmb.eldest; rev; rev = rev->next)
    {
      const char *temp_path;
      apr_pool_t *tmppool;
      apr_hash_t *props;
      apr_file_t *file;
      svn_stream_t *stream;
      apr_array_header_t *prop_diffs;
      svn_txdelta_stream_t *delta_stream;
      svn_txdelta_window_handler_t delta_handler = NULL;
      void *delta_baton = NULL;

      svn_pool_clear(currpool);

      /* Get the contents of the file from the repository, and put them in
         a temporary local file. */
      SVN_ERR(svn_stream_open_unique(&stream, &temp_path, NULL,
                                     svn_io_file_del_on_pool_cleanup,
                                     currpool, currpool));
      SVN_ERR(svn_ra_get_file(ra_session, rev->path + 1, rev->revision,
                              stream, NULL, &props, currpool));
      SVN_ERR(svn_stream_close(stream));

      /* Open up a stream to the local file. */
      SVN_ERR(svn_io_file_open(&file, temp_path, APR_READ, APR_OS_DEFAULT,
                               currpool));
      stream = svn_stream_from_aprfile2(file, FALSE, currpool);

      /* Calculate the property diff */
      SVN_ERR(svn_prop_diffs(&prop_diffs, props, last_props, lastpool));

      /* Call the file_rev handler */
      SVN_ERR(handler(handler_baton, rev->path, rev->revision, rev->props,
                      FALSE, /* merged revision */
                      &delta_handler, &delta_baton, prop_diffs, lastpool));

      /* Compute and send delta if client asked for it. */
      if (delta_handler)
        {
          /* Get the content delta. */
          svn_txdelta(&delta_stream, last_stream, stream, lastpool);

          /* And send. */
          SVN_ERR(svn_txdelta_send_txstream(delta_stream, delta_handler,
                                            delta_baton, lastpool));
        }

      /* Switch the pools and data for the next iteration */
      tmppool = currpool;
      currpool = lastpool;
      lastpool = tmppool;

      svn_stream_close(last_stream);
      last_stream = stream;
      last_props = props;
    }

  svn_stream_close(last_stream);
  svn_pool_destroy(currpool);
  svn_pool_destroy(lastpool);

  /* Reparent the session back to the original URL. */
  return svn_ra_reparent(ra_session, session_url, pool);
}


/*** Fallback implementation of svn_ra_get_deleted_rev(). ***/

/* svn_ra_get_log2() receiver_baton for svn_ra__get_deleted_rev_from_log(). */
typedef struct log_path_del_rev_t
{
  /* Absolute repository path. */
  const char *path;

  /* Revision PATH was first deleted or replaced. */
  svn_revnum_t revision_deleted;
} log_path_del_rev_t;

/* A svn_log_entry_receiver_t callback for finding the revision
   ((log_path_del_rev_t *)BATON)->PATH was first deleted or replaced.
   Stores that revision in ((log_path_del_rev_t *)BATON)->REVISION_DELETED.
 */
static svn_error_t *
log_path_del_receiver(void *baton,
                      svn_log_entry_t *log_entry,
                      apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  /* No paths were changed in this revision.  Nothing to do. */
  if (! log_entry->changed_paths2)
    return SVN_NO_ERROR;

  for (hi = apr_hash_first(pool, log_entry->changed_paths2);
       hi != NULL;
       hi = apr_hash_next(hi))
    {
      void *val;
      char *path;
      svn_log_changed_path_t *log_item;

      apr_hash_this(hi, (void *) &path, NULL, &val);
      log_item = val;
      if (svn_path_compare_paths(((log_path_del_rev_t *) baton)->path,
                                 path) == 0
                                 && (log_item->action == 'D'
                                     || log_item->action == 'R'))
        {
          /* Found the first deletion or replacement, we are done. */
          ((log_path_del_rev_t *) baton)->revision_deleted =
            log_entry->revision;
          break;
        }
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra__get_deleted_rev_from_log(svn_ra_session_t *session,
                                 const char *rel_deleted_path,
                                 svn_revnum_t peg_revision,
                                 svn_revnum_t end_revision,
                                 svn_revnum_t *revision_deleted,
                                 apr_pool_t *pool)
{
  const char *session_url, *source_root_url, *rel_path_url, *abs_del_path;
  log_path_del_rev_t log_path_deleted_baton;

  SVN_ERR_ASSERT(*rel_deleted_path != '/');

  if (!SVN_IS_VALID_REVNUM(peg_revision))
    return svn_error_createf(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                             _("Invalid peg revision %ld"), peg_revision);
  if (!SVN_IS_VALID_REVNUM(end_revision))
    return svn_error_createf(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                             _("Invalid end revision %ld"), end_revision);
  if (end_revision <= peg_revision)
    return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                            _("Peg revision must precede end revision"));

  SVN_ERR(svn_ra_get_session_url(session, &session_url, pool));
  SVN_ERR(svn_ra_get_repos_root2(session, &source_root_url, pool));
  rel_path_url = svn_path_url_add_component(session_url, rel_deleted_path,
                                            pool);
  abs_del_path = svn_path_uri_decode(rel_path_url + strlen(source_root_url), pool);
  log_path_deleted_baton.path = abs_del_path;
  log_path_deleted_baton.revision_deleted = SVN_INVALID_REVNUM;

  /* Examine the logs of SESSION's URL to find when DELETED_PATH was first
     deleted or replaced. */
  SVN_ERR(svn_ra_get_log2(session, NULL, peg_revision, end_revision, 0,
                          TRUE, TRUE, FALSE,
                          apr_array_make(pool, 0, sizeof(char *)),
                          log_path_del_receiver, &log_path_deleted_baton,
                          pool));
  *revision_deleted = log_path_deleted_baton.revision_deleted;
  return SVN_NO_ERROR;
}
