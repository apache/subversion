/* svn_error:  common exception handling for Subversion
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by CollabNet (http://www.Collab.Net)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software may consist of voluntary contributions made by many
 * individuals on behalf of CollabNet.
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

apr_pool_t *
svn_pool_create (apr_pool_t *parent_pool)
{
  apr_pool_t *ret_pool;
  apr_status_t apr_err;
  apr_pool_t *error_pool;

  ret_pool = apr_make_sub_pool (parent_pool, abort_on_pool_failure);

  /* If there is no parent, then initialize ret_pool as the "top". */
  if (parent_pool == NULL)
    {
      parent_pool = ret_pool;
      apr_err = svn_error_init_pool (parent_pool);
      if (apr_err)
        (*abort_on_pool_failure) (apr_err);
    }

  /* Fetch the error pool from the parent (possibly the new one). */
  apr_get_userdata ((void **) &error_pool, SVN_ERROR_POOL, parent_pool);
  if (error_pool == NULL)
    (*abort_on_pool_failure) (SVN_ERR_BAD_CONTAINING_POOL);

  /* Set the error pool on the newly-created pool. */
  apr_err = apr_set_userdata (error_pool,
                              SVN_ERROR_POOL,
                              apr_null_cleanup,
                              ret_pool);
  if (apr_err)
    (*abort_on_pool_failure) (apr_err);

  return ret_pool;
}


/*** Creating and destroying errors. ***/

/*
  svn_error_create() : for creating nested exception structures.

  Input:  an apr_status_t error code,
          the "original" system error code, if applicable,
          a descriptive message,
          a "child" exception,
          a pool for alloc'ing

  Returns:  a new error structure (containing the old one).

 */

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


/* A quick n' easy way to create a wrappered exception with your own
   message, before throwing it up the stack.  (It uses all of the
   child's fields by default.)  */

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


/* Very dumb "default" error handler: Just prints out error stack
   (recursively), and quits if the fatal flag is set.  */
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
    fprintf (stream, "\napr_error: #%d, src_err %d, canonical err %d : %s\n",
             err->apr_err,
             err->src_err,
             apr_canonical_error (err->apr_err),
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
