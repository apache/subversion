/* error.c:  common exception handling for Subversion
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



#include <stdarg.h>
#include <assert.h>

#include <apr_lib.h>
#include <apr_general.h>
#include <apr_pools.h>
#include <apr_strings.h>
#include <apr_hash.h>

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_utf.h"

#ifdef SVN_DEBUG
/* file_line for the non-debug case. */
static const char SVN_FILE_LINE_UNDEFINED[] = "svn:<undefined>";
#endif /* SVN_DEBUG */



/*** Helpers for creating errors ***/
#undef svn_error_create
#undef svn_error_createf
#undef svn_error_quick_wrap


/* XXX FIXME: These should be protected by a thread mutex.
   svn_error__locate and make_error_internal should cooperate
   in locking and unlocking it. */

/* XXX TODO: Define mutex here #if APR_HAS_THREADS */
static const char *error_file = NULL;
static long error_line = -1;

void
svn_error__locate (const char *file, long line)
{
  /* XXX TODO: Lock mutex here */
  error_file = file;
  error_line = line;
}


static svn_error_t *
make_error_internal (apr_status_t apr_err,
                     svn_error_t *child)
{
  apr_pool_t *pool;
  svn_error_t *new_error;

  /* Reuse the child's pool, or create our own. */
  if (child)
    pool = child->pool;
  else if (apr_pool_create (&pool, NULL))
    abort ();

  /* Create the new error structure */
  new_error = (svn_error_t *) apr_pcalloc (pool, sizeof (*new_error));

  /* Fill 'er up. */
  new_error->apr_err = apr_err;
  new_error->child   = child;
  new_error->pool    = pool;
  new_error->file    = error_file;
  new_error->line    = error_line;
  /* XXX TODO: Unlock mutex here */

  return new_error;
}



/*** Creating and destroying errors. ***/

svn_error_t *
svn_error_create (apr_status_t apr_err,
                  svn_error_t *child,
                  const char *message)
{
  svn_error_t *err;

  err = make_error_internal (apr_err, child);

  err->message = (const char *) apr_pstrdup (err->pool, message);

  return err;
}


svn_error_t *
svn_error_createf (apr_status_t apr_err,
                   svn_error_t *child,
                   const char *fmt,
                   ...)
{
  svn_error_t *err;

  va_list ap;

  err = make_error_internal (apr_err, child);

  va_start (ap, fmt);
  err->message = apr_pvsprintf (err->pool, fmt, ap);
  va_end (ap);

  return err;
}


svn_error_t *
svn_error_quick_wrap (svn_error_t *child, const char *new_msg)
{
  return svn_error_create (child->apr_err,
                           child,
                           new_msg);
}


void
svn_error_compose (svn_error_t *chain, svn_error_t *new_err)
{
  apr_pool_t *pool = chain->pool;
  apr_pool_t *oldpool = new_err->pool;

  while (chain->child)
    chain = chain->child;

  /* Copy the new error chain into the old chain's pool. */
  while (new_err)
    {
      chain->child = apr_palloc (pool, sizeof (*chain->child));
      chain = chain->child;
      *chain = *new_err;
      chain->message = apr_pstrdup (pool, new_err->message);
      new_err = new_err->child;
    }

  /* Destroy the new error chain. */
  apr_pool_destroy (oldpool);
}


void
svn_error_clear (svn_error_t *err)
{
  if (err)
    apr_pool_destroy (err->pool);
}


static void
handle_error (svn_error_t *err, FILE *stream, svn_boolean_t fatal,
              int depth, apr_status_t parent_apr_err)
{
  char errbuf[256];
  char utfbuf[2048];
  const char *err_string;

  /* Pretty-print the error */
  /* Note: we can also log errors here someday. */

#ifdef SVN_DEBUG
  if (err->file)
    fprintf (stream, "%s:%ld",
             svn_utf_utf8_to_native (err->file, utfbuf, sizeof (utfbuf)),
             err->line);
  else
    fputs (SVN_FILE_LINE_UNDEFINED, stream);

  fprintf (stream, ": (apr_err=%d)\n", err->apr_err);
#endif /* SVN_DEBUG */

  /* When we're recursing, don't repeat the top-level message if its
     the same as before. */
  if (depth == 0 || err->apr_err != parent_apr_err)
    {
      /* Is this a Subversion-specific error code? */
      if ((err->apr_err > APR_OS_START_USEERR)
          && (err->apr_err <= APR_OS_START_CANONERR))
        err_string = svn_utf_utf8_to_native
          (svn_strerror (err->apr_err, errbuf, sizeof (errbuf)),
           utfbuf, sizeof (utfbuf));
      /* Otherwise, this must be an APR error code. */
      else
        err_string = apr_strerror (err->apr_err, errbuf, sizeof (errbuf));

      fprintf (stream, "svn: %s\n", err_string);
    }
  if (err->message)
    fprintf (stream, "svn: %s\n",
             svn_utf_utf8_to_native (err->message, utfbuf, sizeof (utfbuf)));
  fflush (stream);

  if (err->child)
    handle_error (err->child, stream, FALSE, depth + 1, err->apr_err);

  if (fatal)
    /* XXX Shouldn't we exit(1) here instead, so that atexit handlers
       get called?  --xbc */
    abort ();
}

void
svn_handle_error (svn_error_t *err, FILE *stream, svn_boolean_t fatal)
{
  handle_error (err, stream, fatal, 0, APR_SUCCESS);
}


void
svn_handle_warning (apr_pool_t *pool, void *data, const char *fmt, ...)
{
  va_list ap;
  svn_stringbuf_t *msg, *msg_utf8;
  svn_error_t *err;
  apr_pool_t *subpool = svn_pool_create (pool);
  FILE *stream = data;

  va_start (ap, fmt);
  msg_utf8 = svn_stringbuf_create (apr_pvsprintf (subpool, fmt, ap), subpool);
  va_end (ap);

  err = svn_utf_stringbuf_from_utf8 (&msg, msg_utf8, subpool);

  if (err)
    handle_error (err, stream, FALSE, 0, APR_SUCCESS);
  else
    {
      fprintf (stream, "svn: warning: %s\n", msg->data);
      fflush (stream);
    }

  svn_pool_destroy (subpool);
}



/* svn_strerror() and helpers */

typedef struct {
  svn_errno_t errcode;
  const char *errdesc;
} err_defn;

/* To understand what is going on here, read svn_error_codes.h. */
#define SVN_ERROR_BUILD_ARRAY
#include "svn_error_codes.h"

char *
svn_strerror (apr_status_t statcode, char *buf, apr_size_t bufsize)
{
  const err_defn *defn;

  for (defn = error_table; defn->errdesc != NULL; ++defn)
    if (defn->errcode == statcode)
      {
        apr_cpystrn (buf, defn->errdesc, bufsize);
        return buf;
      }

  return apr_strerror (statcode, buf, bufsize);
}
