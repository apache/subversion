/* svn_error:  common exception handling for Subversion
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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

/* Key for the error pool itself. */
#define SVN_ERROR_POOL              "svn-error-pool"

/* Key for a boolean signifying whether the error pool is a subpool of
   the pool whose prog_data we got it from. */
#define SVN_ERROR_POOL_ROOTED_HERE  "svn-error-pool-rooted-here"



/*** helpers for creating errors ***/

static svn_error_t *
make_error_internal (apr_status_t apr_err,
                     int src_err,
                     svn_error_t *child,
                     apr_pool_t *pool)
{
  svn_error_t *new_error;
  apr_pool_t *newpool;

  /* Make a new subpool of the active error pool, or else use child's pool. */
  if (pool)
    {
      apr_pool_t *error_pool;
      apr_pool_userdata_get ((void **) &error_pool, SVN_ERROR_POOL, pool);
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

  /* Fill 'er up. */
  new_error->apr_err = apr_err;
  new_error->src_err = src_err;
  new_error->child   = child;
  new_error->pool    = newpool;  

  return new_error;
}



/*** Setting a semi-global error pool. ***/

static int
abort_on_pool_failure (int retcode)
{
  abort ();
  return -1; /* prevent compiler warnings */
}


static apr_status_t
svn_error__make_error_pool (apr_pool_t *parent, apr_pool_t **error_pool)
{
  apr_status_t apr_err;

  /* Create a subpool to hold all error allocations. We use a subpool rather
     than the parent itself, so that we can clear the error pool. */
  *error_pool = apr_pool_sub_make (parent, abort_on_pool_failure);
  
  /* Set the error pool on itself. */
  apr_err = apr_pool_userdata_set (*error_pool, SVN_ERROR_POOL, apr_pool_cleanup_null,
                              *error_pool);

  return apr_err;
}


/* Get POOL's error pool into *ERROR_POOL.
 * 
 * If ROOTED_HERE is not null, then
 *   - If the error pool is a direct subpool of POOL, set *ROOTED_HERE to 1
 *   - Else set *ROOTED_HERE to 0
 * Else don't touch *ROOTED_HERE.
 *
 * Abort if POOL does not have an error pool.
 */
static void
svn_error__get_error_pool (apr_pool_t *pool,
                           apr_pool_t **error_pool,
                           svn_boolean_t *rooted_here)
{
  apr_pool_userdata_get ((void **) error_pool, SVN_ERROR_POOL, pool);
  if (*error_pool == NULL)
    abort_on_pool_failure (SVN_ERR_BAD_CONTAINING_POOL);

  if (rooted_here)
    apr_pool_userdata_get ((void *) rooted_here, SVN_ERROR_POOL_ROOTED_HERE, pool);
}


/* Set POOL's error pool to ERROR_POOL.
 *
 * If ROOTED_HERE is non-zero, then record the fact that ERROR_POOL is
 * a direct subpool of POOL.
 *
 * If unable to set either, abort.
 */
static void
svn_error__set_error_pool (apr_pool_t *pool,
                           apr_pool_t *error_pool,
                           svn_boolean_t rooted_here)
{
  apr_status_t apr_err;

  apr_err = apr_pool_userdata_set (error_pool, SVN_ERROR_POOL,
                              apr_pool_cleanup_null, pool);
  if (apr_err)
    abort_on_pool_failure (apr_err);

  if (rooted_here)
    {
      apr_err = apr_pool_userdata_set ((void *) 1, SVN_ERROR_POOL_ROOTED_HERE,
                                  apr_pool_cleanup_null, pool);
      if (apr_err)
        abort_on_pool_failure (apr_err);
    }
}


/* Set P's error pool to that of P's parent.  P's parent must exist
   and have an error pool, else we abort. */
static void
svn_pool__inherit_error_pool (apr_pool_t *p)
{
  apr_pool_t *error_pool;

  if (p->parent == NULL)
    abort_on_pool_failure (SVN_ERR_BAD_CONTAINING_POOL);

  svn_error__get_error_pool (p->parent, &error_pool, NULL);
  svn_error__set_error_pool (p, error_pool, 0);
}


apr_status_t
svn_error_init_pool (apr_pool_t *top_pool)
{
  apr_pool_t *error_pool;
  apr_status_t apr_err;

  apr_err = svn_error__make_error_pool (top_pool, &error_pool);
  if (! APR_STATUS_IS_SUCCESS (apr_err))
    return apr_err;

  svn_error__set_error_pool (top_pool, error_pool, 1);

  return 0;
}


apr_size_t
svn_pool_get_size (apr_pool_t *p)
{
  apr_size_t num_bytes = 0;
  
  /* Do nothing if P is NULL */
  if (p == NULL)
    return 0;

  num_bytes = apr_pool_num_bytes (p);
  if (p->sub_pools)
    {
      apr_pool_t *this_pool = p->sub_pools;
      num_bytes += svn_pool_get_size (this_pool);
      while (this_pool->sub_next)
        {
          this_pool = this_pool->sub_next;
          num_bytes += svn_pool_get_size (this_pool);
        }
    }
  return num_bytes;
}


#ifdef SVN_POOL_DEBUG
/* Find the oldest living ancestor of pool P (which could very well be
   P itself) */
static apr_pool_t *
find_oldest_pool_ancestor (apr_pool_t *p)
{
  apr_pool_t *ret_pool = p;

  if (ret_pool != NULL)
    {
      while (ret_pool->parent)
        ret_pool = ret_pool->parent;
    }
  return ret_pool;
}
#endif /* SVN_POOL_DEBUG */



#ifndef SVN_POOL_DEBUG
apr_pool_t *
svn_pool_create (apr_pool_t *parent_pool)
#else /* SVN_POOL_DEBUG */
apr_pool_t *
svn_pool_create_debug (apr_pool_t *parent_pool,
                       const char *file,
                       int line)
#endif /* SVN_POOL_DEBUG */
{
  apr_pool_t *ret_pool;

  ret_pool = apr_pool_sub_make (parent_pool, abort_on_pool_failure);

  /* If there is no parent, then initialize ret_pool as the "top". */
  if (parent_pool == NULL)
    {
      apr_status_t apr_err;

      apr_err = svn_error_init_pool (ret_pool);
      if (apr_err)
        abort_on_pool_failure (apr_err);
    }
  else
    svn_pool__inherit_error_pool (ret_pool);

#ifdef SVN_POOL_DEBUG
  {
    fprintf (stderr, "Pool 0x%08X created at %s:%d\n", 
             (unsigned int)ret_pool, file, line);
  }
#endif /* SVN_POOL_DEBUG */

  return ret_pool;
}


#ifndef SVN_POOL_DEBUG
void 
svn_pool_clear (apr_pool_t *p)
#else /* SVN_POOL_DEBUG */
void 
svn_pool_clear_debug (apr_pool_t *p,
                      const char *file,
                      int line)
#endif /* SVN_POOL_DEBUG */
{
  apr_pool_t *error_pool;
  svn_boolean_t subpool_of_p_p;  /* That's "predicate" to you, bud. */
    
#ifdef SVN_POOL_DEBUG
  {
    apr_size_t num_bytes = svn_pool_get_size (p);
    apr_size_t global_num_bytes = 
      svn_pool_get_size (find_oldest_pool_ancestor (p));
    
    fprintf (stderr, "Pool 0x%08X cleared at %s:%d (%d/%d bytes)\n", 
             (unsigned int)p, file, line, num_bytes, global_num_bytes);
  }
#endif /* SVN_POOL_DEBUG */

  if (p->parent)
    svn_error__get_error_pool (p->parent, &error_pool, &subpool_of_p_p);
  else
    {
      error_pool = NULL;    /* Paranoia. */
      subpool_of_p_p = 1;   /* The only possibility. */
    }

  apr_pool_clear (p);

  /* Clearing the pool invalidated all userdata attached to it,
     so we must reattach its error pool.  However, clearing a pool
     also destroys all its subpools; if the error pool was a subpool
     of p (meaning it was created for p, rather than inherited from
     p's parent), then we must conjure up a new error pool here.
     Otherwise, we should stick with the old one, no point creating a
     new error pool when we have a perfectly good one at hand. */

  if (subpool_of_p_p)
    svn_error__make_error_pool (p, &error_pool);

  svn_error__set_error_pool (p, error_pool, subpool_of_p_p);
}


#ifndef SVN_POOL_DEBUG
void
svn_pool_destroy (apr_pool_t *p)
#else /* SVN_POOL_DEBUG */
void
svn_pool_destroy_debug (apr_pool_t *p,
                        const char *file,
                        int line)
#endif /* SVN_POOL_DEBUG */
{
#ifdef SVN_POOL_DEBUG
  {
    apr_size_t num_bytes = svn_pool_get_size (p);
    apr_size_t global_num_bytes = 
      svn_pool_get_size (find_oldest_pool_ancestor (p));
    
    fprintf (stderr, "Pool 0x%08X destroyed at %s:%d (%d/%d bytes)\n", 
             (unsigned int)p, file, line, num_bytes, global_num_bytes);
  }
#endif /* SVN_POOL_DEBUG */

  apr_pool_destroy (p);
}



/*** Creating and destroying errors. ***/

svn_error_t *
svn_error_create (apr_status_t apr_err,
                  int src_err,
                  svn_error_t *child,
                  apr_pool_t *pool,
                  const char *message)
{
  svn_error_t *err;

  err = make_error_internal (apr_err, src_err, child, pool);
  
  err->message = (const char *) apr_pstrdup (err->pool, message);

  return err;
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

  err = make_error_internal (apr_err, src_err, child, pool);

  va_start (ap, fmt);
  err->message = apr_pvsprintf (err->pool, fmt, ap);
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
  svn_pool_destroy (err->pool);
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

  if (err->message)
    fprintf (stream, "  %s", err->message);

  fputc ('\n', stream);
  fflush (stream);

  if (err->child)
    svn_handle_error (err->child, stream, 0);

  if (fatal)
    abort ();
}



void 
svn_handle_warning (void *data, const char *fmt, ...)
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
