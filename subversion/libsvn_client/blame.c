/*
 * blame.c:  return blame messages
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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

#include "client.h"

#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_diff.h"
#include "svn_pools.h"
#include "svn_path.h"

/* The metadata associated with a particular revision. */
struct rev
{
  svn_revnum_t revision; /* the revision number */
  const char *author;    /* the author of the revision */
  const char *date;      /* the date of the revision */
  const char *path;      /* the absolute repository path */
  struct rev *next;      /* the next revision */
};

/* One chunk of blame */
struct blame
{
  struct rev *rev;    /* the responsible revision */
  apr_off_t start;    /* the starting diff-token (line) */
  struct blame *next; /* the next chunk */
};

/* The baton used for svn_diff operations */
struct diff_baton
{
  struct rev *rev;     /* the rev for which blame is being assigned */
  struct blame *blame; /* linked list of blame chunks */
  struct blame *avail; /* linked list of free blame chunks */
  apr_pool_t *pool;
};

/* Create a blame chunk associated with REV for a change starting
   at token START. */
static struct blame *
blame_create (struct diff_baton *baton, struct rev *rev, apr_off_t start)
{
  struct blame *blame;
  if (baton->avail)
    {
      blame = baton->avail;
      baton->avail = blame->next;
    }
  else
    blame = apr_palloc (baton->pool, sizeof (*blame));
  blame->rev = rev;
  blame->start = start;       
  blame->next = NULL;
  return blame;
}

/* Destroy a blame chunk. */
static void
blame_destroy (struct diff_baton *baton, struct blame *blame)
{
  blame->next = baton->avail;
  baton->avail = blame;
}

/* Return the blame chunk that contains token OFF, starting the search at
   BLAME. */
static struct blame *
blame_find (struct blame *blame, apr_off_t off)
{
  struct blame *prev = NULL;
  while (blame)
    {
      if (blame->start > off) break;
      prev = blame;
      blame = blame->next;
    }
  return prev;
}

/* Shift the start-point of BLAME and all subsequence blame-chunks
   by ADJUST tokens */
static void
blame_adjust (struct blame *blame, apr_off_t adjust)
{
  while (blame)
    {
      blame->start += adjust;
      blame = blame->next;
    }
}

/* Delete the blame associated with the region from token START to
   START + LENGTH */
static svn_error_t *
blame_delete_range (struct diff_baton *db, apr_off_t start, apr_off_t length)
{
  struct blame *first = blame_find (db->blame, start);
  struct blame *last = blame_find (db->blame, start + length);
  struct blame *tail = last->next;

  if (first != last)
    {
      struct blame *walk = first->next;
      while (walk != last)
        {
          struct blame *next = walk->next;
          blame_destroy (db, walk);
          walk = next;
        }
      first->next = last;
      last->start = start;
      if (first->start == start)
        {
          *first = *last;
          blame_destroy (db, last);
          last = first;
        }
    }

  if (tail && tail->start == last->start + length)
    {
      *last = *tail;
      blame_destroy (db, tail);
      tail = last->next;
    }

  blame_adjust (tail, -length);

  return SVN_NO_ERROR;
}

/* Insert a chunk of blame associated with DB->REV starting
   at token START and continuing for LENGTH tokens */
static svn_error_t *
blame_insert_range (struct diff_baton *db, apr_off_t start, apr_off_t length)
{
  struct blame *head = db->blame;
  struct blame *point = blame_find (head, start);
  struct blame *insert;

  if (point->start == start)
    {
      insert = blame_create (db, point->rev, point->start + length);
      point->rev = db->rev;
      insert->next = point->next;
      point->next = insert;
    }
  else if (!point->next || point->next->start > start + length)
    {
      struct blame *middle;
      middle = blame_create (db, db->rev, start);
      insert = blame_create (db, point->rev, start + length);
      middle->next = insert;
      insert->next = point->next;
      point->next = middle;
    }
  else
    {
      insert = blame_create (db, db->rev, start);
      insert->next = point->next;
      point->next = insert;
    }
  blame_adjust (insert->next, length);

  return SVN_NO_ERROR;
}

/* Callback for diff between subsequent revisions */
static svn_error_t *
output_diff_modified (void *baton,
                      apr_off_t original_start,
                      apr_off_t original_length,
                      apr_off_t modified_start,
                      apr_off_t modified_length,
                      apr_off_t latest_start,
                      apr_off_t latest_length)
{
  struct diff_baton *db = baton;

  if (original_length)
    SVN_ERR (blame_delete_range (db, modified_start, original_length));

  if (modified_length)
    SVN_ERR (blame_insert_range (db, modified_start, modified_length));

  return SVN_NO_ERROR;
}

/* The baton used for RA->get_log */
struct log_message_baton {
  const char *path;        /* The path to be processed */
  struct rev *eldest;      /* The eldest revision processed */
  svn_cancel_func_t cancel_func; /* cancellation callback */ 
  void *cancel_baton;            /* cancellation baton */
  apr_pool_t *pool; 
};

const svn_diff_output_fns_t output_fns = {
        NULL,
        output_diff_modified
};
                     
/* Callback for log messages: accumulates revision metadata into
   a chronologically ordered list stored in the baton. */
static svn_error_t *
log_message_receiver (void *baton,
                      apr_hash_t *changed_paths,
                      svn_revnum_t revision,
                      const char *author,
                      const char *date,
                      const char *message,
                      apr_pool_t *pool)
{
  svn_log_changed_path_t *change;
  struct log_message_baton *lmb = baton;
  struct rev *rev;

  if (lmb->cancel_func)
    SVN_ERR (lmb->cancel_func (lmb->cancel_baton));

  rev = apr_palloc (lmb->pool, sizeof (*rev));
  rev->revision = revision;
  rev->author = apr_pstrdup (lmb->pool, author);
  rev->date = apr_pstrdup (lmb->pool, date);
  rev->path = lmb->path;
  rev->next = lmb->eldest;
  lmb->eldest = rev;

  /* This path was either explicitly changed, or part of a directory
     operation.  In the former case, it will have a changed_paths
     entry of its own.  Otherwise, it gets a little messy; we need
     to figure out which parent directory was involved. */
  change = apr_hash_get (changed_paths, lmb->path, APR_HASH_KEY_STRING);
  if (change != NULL)
    {
      if (change->copyfrom_path)
        lmb->path = apr_pstrdup (lmb->pool, change->copyfrom_path);
    }
  else
    {
      apr_hash_index_t *hi;
      const void *key;
      void *val;
      int len;
      for (hi = apr_hash_first (pool, changed_paths); hi;
           hi = apr_hash_next (hi))
        {
          const char *path;
          apr_hash_this (hi, &key, NULL, &val);
          path = key;
          change = val;
          len = strlen (path);

          if (strncmp (path, lmb->path, len) == 0 && lmb->path[len] == '/')
            break;
        }

      if (! change || !change->copyfrom_path)
        return svn_error_createf (APR_EGENERAL, NULL,
                                  "Missing changed-path information for "
                                  "revision %"SVN_REVNUM_T_FMT" of '%s'",
                                  rev->revision, rev->path);

      lmb->path = svn_path_join (change->copyfrom_path, lmb->path + len + 1,
                                 lmb->pool);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_blame (const char *target,
                  const svn_opt_revision_t *start,
                  const svn_opt_revision_t *end,
                  svn_boolean_t strict_node_history,
                  svn_client_blame_receiver_t receiver,
                  void *receiver_baton,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  const char *reposURL;
  struct log_message_baton lmb;
  apr_array_header_t *condensed_targets;
  svn_ra_plugin_t *ra_lib; 
  void *ra_baton, *session;
  const char *url;
  const char *auth_dir;
  svn_revnum_t start_revnum, end_revnum;
  struct blame *walk;
  apr_file_t *file;
  svn_stream_t *stream;
  apr_pool_t *iterpool;
  struct rev *rev;
  apr_status_t apr_err;
  svn_node_kind_t kind;
  struct diff_baton db;
  const char *last = NULL;

  if (start->kind == svn_opt_revision_unspecified
      || end->kind == svn_opt_revision_unspecified)
    return svn_error_create
      (SVN_ERR_CLIENT_BAD_REVISION, NULL,
       "svn_client_blame: caller failed to supply revisions");

  iterpool = svn_pool_create (pool);

  SVN_ERR (svn_client_url_from_path (&url, target, pool));
  if (! url)
    return svn_error_createf (SVN_ERR_ENTRY_MISSING_URL, NULL,
                              "'%s' has no URL", target);


  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, url, pool));

  SVN_ERR (svn_client__dir_if_wc (&auth_dir, "", pool));

  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, url, auth_dir,
                                        NULL, NULL, FALSE, FALSE,
                                        ctx, pool));

  SVN_ERR (svn_client__get_revision_number (&start_revnum, ra_lib, session,
                                            start, target, pool));
  SVN_ERR (svn_client__get_revision_number (&end_revnum, ra_lib, session,
                                            end, target, pool));

  if (end_revnum < start_revnum)
    return svn_error_create
      (SVN_ERR_CLIENT_BAD_REVISION, NULL,
       "svn_client_blame: start revision must preceed end revision");

  SVN_ERR (ra_lib->check_path (session, "", end_revnum, &kind, pool));

  if (kind == svn_node_dir)
    return svn_error_createf (SVN_ERR_CLIENT_IS_DIRECTORY, NULL,
                              "URL \"%s\" refers to directory", url);

  condensed_targets = apr_array_make (pool, 1, sizeof (const char *));
  (*((const char **)apr_array_push (condensed_targets))) = "";

  SVN_ERR (ra_lib->get_repos_root (session, &reposURL, pool));

  lmb.path = url + strlen (reposURL);
  lmb.cancel_func = ctx->cancel_func;
  lmb.cancel_baton = ctx->cancel_baton;
  lmb.eldest = NULL;
  lmb.pool = pool;

  SVN_ERR (ra_lib->get_log (session,
                            condensed_targets,
                            end_revnum,
                            start_revnum,
                            TRUE,
                            strict_node_history,
                            log_message_receiver,
                            &lmb,
                            pool));

  if (! lmb.eldest)
    return SVN_NO_ERROR;

  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, reposURL, auth_dir,
                                        NULL, NULL, FALSE, FALSE,
                                        ctx, pool));

  db.avail = NULL;
  db.pool = pool;

  for (rev = lmb.eldest; rev; rev = rev->next)
    {
      const char *tmp;
      SVN_ERR (svn_io_open_unique_file (&file, &tmp, "", ".tmp",
                                        FALSE, pool));
      stream = svn_stream_from_aprfile (file, iterpool);
      SVN_ERR (ra_lib->get_file (session, rev->path + 1, rev->revision,
                                 stream, NULL, NULL, iterpool));
      SVN_ERR (svn_stream_close (stream));
      apr_err = apr_file_close (file);
      if (apr_err != APR_SUCCESS)
        return svn_error_createf (apr_err, NULL, "error closing %s", 
                                  rev->path);
      if (last)
        {
          svn_diff_t *diff;
          db.rev = rev;
          apr_pool_clear (iterpool);
          SVN_ERR (svn_diff_file_diff (&diff, last, tmp, iterpool));
          SVN_ERR (svn_diff_output (diff, &db, &output_fns));
          apr_err = apr_file_remove (last, iterpool);
          if (apr_err != APR_SUCCESS)
            return svn_error_createf (apr_err, NULL, "error removing %s", 
                                      last);
        }
      else
        db.blame = blame_create (&db, rev, 0);

      last = tmp;
    }

  apr_err = apr_file_open (&file, last, APR_READ, APR_OS_DEFAULT, pool);
  if (apr_err != APR_SUCCESS)
    return svn_error_createf (apr_err, NULL, "error opening %s", last);

  stream = svn_stream_from_aprfile (file, pool);
  for (walk = db.blame; walk; walk = walk->next)
    {
      int i;
      for (i = walk->start; !walk->next || i < walk->next->start; i++)
        {
          svn_stringbuf_t *sb;
          apr_pool_clear (iterpool);
          SVN_ERR (svn_stream_readline (stream, &sb, iterpool));
          if (! sb)
            break;
          SVN_ERR (receiver (receiver_baton, i, walk->rev->revision,
                             walk->rev->author, walk->rev->date,
                             sb->len ? sb->data : "", iterpool));
        }
    }

  SVN_ERR (svn_stream_close (stream));
  apr_err = apr_file_close (file);
  if (apr_err != APR_SUCCESS)
    return svn_error_createf (apr_err, NULL, "error closing %s", last);
  apr_err = apr_file_remove (last, pool);
  if (apr_err != APR_SUCCESS)
    return svn_error_createf (apr_err, NULL, "error removing %s", last);

  apr_pool_destroy (iterpool);
  return SVN_NO_ERROR;
}
