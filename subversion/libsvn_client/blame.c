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

/* The metadata associated with a particular revision. */
struct rev
{
  svn_revnum_t revision; /* the revision number */
  const char *author;    /* the author of the revision */
  const char *date;      /* the date of the revision */
  const char *path;      /* the path of the (temporary) fulltext */
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
  struct rev *last;        /* The last revision processed */
  int rev_count;           /* The number of revs seen so far */
  struct diff_baton db;    /* The baton used for diff operations */
  void *session;           /* The ra session baton */
  svn_ra_plugin_t *ra_lib; /* The ra_lib handle */
  apr_pool_t *diffpool;    /* Pool used for diff operations: cleared
                              between each revision! */
  svn_cancel_func_t cancel_func; /* cancellation callback */ 
  void *cancel_baton;            /* cancellation baton */
  apr_pool_t *pool; 
};

const svn_diff_output_fns_t output_fns = {
        NULL,
        output_diff_modified
};

/* Callback for log messages; creates temporary fulltexts for each
   revision, svn_diffs them with their prevision revision, and
   keeps a running table of blame information as the diffs progress.  */
static svn_error_t *
log_message_receiver (void *baton,
                      apr_hash_t *changed_paths,
                      svn_revnum_t revision,
                      const char *author,
                      const char *date,
                      const char *message,
                      apr_pool_t *pool)
{
  struct log_message_baton *lmb = baton;
  struct rev *last = lmb->last;
  int rev_count = lmb->rev_count++;
  apr_status_t apr_err;
  struct rev *rev;
  svn_diff_t *diff;
  apr_file_t *tmp;
  svn_stream_t *out;

  if (lmb->cancel_func)
    SVN_ERR (lmb->cancel_func (lmb->cancel_baton));

  rev = apr_palloc (lmb->pool, sizeof (*rev));
  rev->revision = revision;
  rev->author = apr_pstrdup (lmb->pool, author);
  rev->date = apr_pstrdup (lmb->pool, date);
  lmb->last = rev;

  SVN_ERR (svn_io_open_unique_file (&tmp, &rev->path, "",
                                    rev_count & 1? ".otmp": ".etmp",
                                    FALSE, lmb->pool));

  out = svn_stream_from_aprfile (tmp, pool);
  SVN_ERR (lmb->ra_lib->get_file (lmb->session, "", revision, out,
                                  NULL, NULL, lmb->pool));

  SVN_ERR (svn_stream_close (out));

  apr_err = apr_file_close (tmp);
  if (apr_err != APR_SUCCESS)
    return svn_error_createf (apr_err, NULL, "error closing %s", 
                              rev->path);
  if (! last)
    {
      lmb->db.blame = blame_create (&lmb->db, rev, 0);
      return SVN_NO_ERROR;
    }

  lmb->db.rev = rev;
  apr_pool_clear (lmb->diffpool);
  SVN_ERR (svn_diff_file_diff (&diff, last->path, rev->path, lmb->diffpool));
  SVN_ERR (svn_diff_output (diff, &lmb->db, &output_fns));

  apr_err = apr_file_remove (last->path, pool);
  if (apr_err != APR_SUCCESS)
    return svn_error_createf (apr_err, NULL, "error removing %s", 
                              last->path);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_blame (const char *path_or_url,
                  const svn_opt_revision_t *start,
                  const svn_opt_revision_t *end,
                  svn_boolean_t strict_node_history,
                  svn_client_blame_receiver_t receiver,
                  void *receiver_baton,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  struct log_message_baton lmb;
  apr_array_header_t *condensed_targets;
  svn_ra_plugin_t *ra_lib;  
  void *ra_baton, *log_session, *text_session;
  const char *url;
  const char *auth_dir;
  svn_revnum_t start_revnum, end_revnum;
  struct blame *walk;
  apr_file_t *last_file;
  svn_stream_t *last_stream;
  apr_pool_t *subpool;
  apr_status_t apr_err;


  if (start->kind == svn_opt_revision_unspecified
      || end->kind == svn_opt_revision_unspecified)
    return svn_error_create
      (SVN_ERR_CLIENT_BAD_REVISION, NULL,
       "svn_client_blame: caller failed to supply revisions");

  subpool = svn_pool_create (pool);

  SVN_ERR (svn_client_url_from_path (&url, path_or_url, subpool));
  if (! url)
    return svn_error_createf (SVN_ERR_ENTRY_MISSING_URL, NULL,
                              "'%s' has no URL", path_or_url);


  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, subpool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, url, subpool));

  SVN_ERR (svn_client__dir_if_wc (&auth_dir, "", subpool));

  SVN_ERR (svn_client__open_ra_session (&log_session, ra_lib, url, auth_dir,
                                        NULL, NULL, FALSE, FALSE,
                                        ctx, subpool));
  SVN_ERR (svn_client__open_ra_session (&text_session, ra_lib, url, auth_dir,
                                        NULL, NULL, FALSE, FALSE,
                                        ctx, subpool));

  SVN_ERR (svn_client__get_revision_number (&start_revnum, ra_lib, log_session,
                                            start, path_or_url, subpool));
  SVN_ERR (svn_client__get_revision_number (&end_revnum, ra_lib, log_session,
                                            end, path_or_url, subpool));

  condensed_targets = apr_array_make (subpool, 1, sizeof (const char *));
  (*((const char **)apr_array_push (condensed_targets))) = "";

  lmb.cancel_func = ctx->cancel_func;
  lmb.cancel_baton = ctx->cancel_baton;
  lmb.last = NULL;
  lmb.rev_count = 0;
  lmb.session = text_session;
  lmb.ra_lib = ra_lib;
  lmb.pool = subpool;
  lmb.diffpool = svn_pool_create (pool);
  lmb.db.rev = NULL;
  lmb.db.blame = NULL;
  lmb.db.avail = NULL;
  lmb.db.pool = subpool;

  SVN_ERR (ra_lib->get_log (log_session,
                            condensed_targets,
                            start_revnum,
                            end_revnum,
                            TRUE,
                            strict_node_history,
                            log_message_receiver,
                            &lmb,
                            subpool));

  apr_pool_destroy (lmb.diffpool);

  if (! lmb.rev_count)
    return SVN_NO_ERROR;
    
  apr_err = apr_file_open (&last_file, lmb.last->path, APR_READ,
                           APR_OS_DEFAULT, subpool);
  if (apr_err != APR_SUCCESS)
    return svn_error_createf (apr_err, NULL, "error opening %s", 
                              lmb.last->path);

  last_stream = svn_stream_from_aprfile (last_file, subpool);
  for (walk = lmb.db.blame; walk; walk = walk->next)
    {
      int i;
      for (i = walk->start; !walk->next || i < walk->next->start; i++)
        {
          svn_stringbuf_t *sb;
          SVN_ERR (svn_stream_readline (last_stream, &sb, subpool));
          if (! sb)
            break;
          SVN_ERR (receiver (receiver_baton, i, walk->rev->revision,
                             walk->rev->author, walk->rev->date,
                             sb->len ? sb->data : "", subpool));
        }
    }

  SVN_ERR (svn_stream_close (last_stream));
  apr_err = apr_file_close (last_file);
  if (apr_err != APR_SUCCESS)
    return svn_error_createf (apr_err, NULL, "error closing %s", 
                              lmb.last->path);
  apr_err = apr_file_remove (lmb.last->path, subpool);
  if (apr_err != APR_SUCCESS)
    return svn_error_createf (apr_err, NULL, "error removing %s", 
                              lmb.last->path);
  apr_pool_destroy (subpool);
  return SVN_NO_ERROR;
}
