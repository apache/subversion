/* svn_error:  common exception handling for Subversion
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */



#include <stdarg.h>
#include <assert.h>
#include "apr_lib.h"
#include "apr_general.h"
#include "apr_pools.h"
#include "apr_strings.h"
#include "svn_error.h"

#define SVN_ERROR_POOL "svn-error-pool"



/*** helpers for creating errors ***/

static svn_error_t *
make_error_internal (apr_status_t apr_err,
                     int src_err,
                     svn_error_t *child,
                     apr_pool_t *pool,
                     const char *message,
                     va_list ap)
{
  svn_error_t *new_error;
  apr_pool_t *newpool;
  char *permanent_msg;

  /* Make a new subpool of the active error pool, or else use child's pool. */
  if (pool)
    {
      apr_pool_t *error_pool;
      apr_get_userdata ((void **) &error_pool, SVN_ERROR_POOL, pool);
      if (error_pool)
        newpool = svn_pool_create (error_pool);
      else
        newpool = pool;
    }
  else if (child)
    newpool = child->pool;
  else            /* can't happen */
    return NULL;  /* kff todo: is this a good reaction? */

  assert (newpool != NULL);

  /* Create the new error structure */
  new_error = (svn_error_t *) apr_pcalloc (newpool, sizeof (*new_error));

  /* Copy the message to permanent storage. */
  if (ap)
    permanent_msg = apr_pvsprintf (newpool, message, ap);
  else
    {
      permanent_msg = apr_palloc (newpool, (strlen (message) + 1));
      strcpy (permanent_msg, message);
    }

  /* Fill 'er up. */
  new_error->apr_err = apr_err;
  new_error->src_err = src_err;
  new_error->message = permanent_msg;
  new_error->child   = child;
  new_error->pool    = newpool;  

  return new_error;
}



/*** Setting a semi-global error pool. ***/

static int
abort_on_pool_failure (int retcode)
{
  abort ();
}

apr_status_t
svn_error_init_pool (apr_pool_t *top_pool)
{
  apr_pool_t *error_pool;
  apr_status_t apr_err;

  /* Create a subpool to hold all error allocations. We use a subpool rather
     than the parent itself, so that we can clear the error pool. */
  error_pool = apr_make_sub_pool (top_pool, abort_on_pool_failure);

  /* Set the error pool on itself. */
  apr_err = apr_set_userdata (error_pool, SVN_ERROR_POOL, apr_null_cleanup,
                              error_pool);
  if (apr_err != APR_SUCCESS)
    return apr_err;

  /* Set the error pool on the top-most pool */
  return apr_set_userdata (error_pool, SVN_ERROR_POOL, apr_null_cleanup,
                           top_pool);
}

static void
svn_pool__attach_error_pool(apr_pool_t *p)
{
  apr_pool_t *error_pool;
  apr_status_t apr_err;
  apr_pool_t *parent_pool;

  if (p->parent == NULL)
    parent_pool = p;
  else
    parent_pool = p->parent;

  /* Fetch the error pool from the parent (possibly the new one). */
  apr_get_userdata ((void **) &error_pool, SVN_ERROR_POOL, parent_pool);
  if (error_pool == NULL)
    abort_on_pool_failure (SVN_ERR_BAD_CONTAINING_POOL);

  /* Set the error pool on the newly-created pool. */
  apr_err = apr_set_userdata (error_pool,
                              SVN_ERROR_POOL,
                              apr_null_cleanup,
                              p);
  if (apr_err)
    abort_on_pool_failure (apr_err);
}


apr_pool_t *
svn_pool_create (apr_pool_t *parent_pool)
{
  apr_pool_t *ret_pool;

  ret_pool = apr_make_sub_pool (parent_pool, abort_on_pool_failure);

  /* If there is no parent, then initialize ret_pool as the "top". */
  if (parent_pool == NULL)
    {
      apr_status_t apr_err;

      apr_err = svn_error_init_pool (ret_pool);
      if (apr_err)
        abort_on_pool_failure (apr_err);
    }

  svn_pool__attach_error_pool (ret_pool);
  
  return ret_pool;
}



void 
svn_pool_clear(apr_pool_t *p)
{
  apr_clear_pool (p);
  /* Clearing the pool, invalidates all userdata attached to the pool,
	 so reattach the error pool. */
  
  svn_pool__attach_error_pool (p);
}


/*** Creating and destroying errors. ***/

svn_error_t *
svn_error_create (apr_status_t apr_err,
                  int src_err,
                  svn_error_t *child,
                  apr_pool_t *pool,
                  const char *message)
{
  return make_error_internal (apr_err, src_err, child, pool, message, NULL);
}


svn_error_t *
svn_error_createf (apr_status_t apr_err,
                   int src_err,
                   svn_error_t *child,
                   apr_pool_t *pool,
                   const char *fmt,
                   ...)
{
  svn_error_t *err;

  va_list ap;
  va_start (ap, fmt);
  err = make_error_internal (apr_err, src_err, child, pool, fmt, ap);
  va_end (ap);

  return err;
}


svn_error_t *
svn_error_quick_wrap (svn_error_t *child, const char *new_msg)
{
  return svn_error_create (child->apr_err,
                           child->src_err,
                           child,
                           NULL,   /* allocate directly in child's pool */
                           new_msg);
}


void
svn_error_free (svn_error_t *err)
{
  apr_destroy_pool (err->pool);
}


void
svn_handle_error (svn_error_t *err, FILE *stream, svn_boolean_t fatal)
{
  char buf[100];

  /* Pretty-print the error */
  /* Note: we can also log errors here someday. */

  /* Is this a Subversion-specific error code? */
  if ((err->apr_err > APR_OS_START_USEERR) 
      && (err->apr_err <= APR_OS_START_CANONERR))
    fprintf (stream, "\nsvn_error: #%d ", err->apr_err);

  /* Otherwise, this must be an APR error code. */
  else
    fprintf (stream, "\napr_error: #%d, src_err %d : %s\n",
             err->apr_err,
             err->src_err,
             apr_strerror (err->apr_err, buf, sizeof(buf)));

  fprintf (stream, "  %s\n", err->message);
  fflush (stream);

  if (err->child)
    svn_handle_error (err->child, stream, 0);

  if (fatal)
    abort ();
}



void 
svn_handle_warning (void *data, char *fmt, ...)
{
  va_list ap;

  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);

  fprintf (stderr, "\n");
  fflush (stderr);
}





/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: 
 */
