/* svn_pools.h:  APR pool management for Subversion
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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




#ifndef SVN_POOLS_H
#define SVN_POOLS_H

#include <apr.h>
#include <apr_errno.h>     /* APR's error system */
#include <apr_pools.h>

#define APR_WANT_STDIO
#include <apr_want.h>

#include <svn_types.h>
#include "svn_io.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/*** Wrappers around APR pools, so we get error pools. ***/

/* If you want pool usage debug info dumped to stderr (in environments
 * that support that kind of thing), #define SVN_POOL_DEBUG here.
 *
 * Output looks like one of these three:
 *
 *    PDEBUG: +                       0xHHHHHHHH (FILE:LINE) parent=0xPPPPPPPP
 *    PDEBUG: 0 SSSSSSSSSS TTTTTTTTTT 0xHHHHHHHH (FILE:LINE) 
 *    PDEBUG: - SSSSSSSSSS TTTTTTTTTT 0xHHHHHHHH (FILE:LINE) 
 *
 * where:
 *
 *    '+' signifies the creation of a pool
 *    '0' signifies the clearing of a pool
 *    '-' signifies the destruction of a pool
 *    SSSSSSSSSS is the decimal size in bytes of the pool
 *    TTTTTTTTTT is the total allocation of that pool tree at the time
 *    0xHHHHHHHH is the address of the pool
 *    0xPPPPPPPP is the address of the pool's parent pool
 */
/*
#define SVN_POOL_DEBUG 
*/



/* THE ERROR POOL
 *
 * When SVN allocates an svn_error_t, it must do so from a pool. There is
 * almost always a pool available for a function to provide to the error
 * creation functions. However, that pool may have a shorter lifetime than
 * is required for the error (in many cases, the error needs to bubble all
 * the way to the top-most control function). Assuming that these shorter-
 * lifetime pools are cleared or even destroyed as the error propagates,
 * then we need a way to ensure that the error is allocated within the
 * proper pool, to get the proper lifetime.
 *
 * We create a pool specifically for errors. This pool is then "hung from"
 * the top-most pool that SVN will be using (whether this top-most pool is
 * provided by an embeddor such as Apache, or whether an SVN tool creates
 * the top-most pool itself). Since this error pool has a lifetime *at least*
 * as long as the top-most pool, then any errors allocated within it will
 * survive back to the top-most control function.
 *
 * We use a subpool rather than the top-most pool itself because we may want
 * to occasionally clear the error pool (say, if we get an error, recover,
 * and restart the operation).
 *
 * This subpool is called "the error pool". Using APR's "userdata" feature,
 * we associate the error pool with every subpool that SVN creates. When
 * the SVN error system allocates a new error, it first fetches the subpool
 * from the pool..
 */

/* You may be wondering why is this is in svn_error, instead of
   svn_pool or whatever.  The reason is the needs of the SVN error
   system are our only justification for wrapping APR's pool creation
   funcs -- because errors have to live as long as the top-most pool
   in a test program or a `request'.  If you're not using SVN errors,
   there's no reason not to use APR's native pool interface.  But you
   are using SVN errors, aren't you? */

/* Initalize the given pool as SVN's top-most pool. This is needed when SVN
 * is embedded in another application, and all of SVN's work will occur
 * within a given pool.
 *
 * This function will construct the error pool (for all errors to live
 * within), and hang it off of the given pool.  It will also create a
 * feedback stream (for the transmission of messages in a
 * non-terminating fashion).  When subpools are created with
 * svn_pool_create(), they will inherit these two things.
 *
 * Note: we return an apr_status_t since a catch-22 means we cannot allocate
 * an svn_error_t.
 *
 * WARNING: this is ONLY to be used for pools provided by an embeddor. Do not
 * use it for pools returned by svn_pool_create().  */
apr_status_t svn_error_init_pool (apr_pool_t *top_pool);


/* The convention here is that the recipient of the feedback has the
   option of returning an APR error value that indicates whether or
   not the calling code should treat the feedback condition as a fatal
   situation. */
typedef struct svn_pool_feedback_t
{
  /* Report items present in the working copy that are apparently not
     under revision control, a'la CVS's "? foobar.c" */
  apr_status_t (*report_unversioned_item) (const char *path);

  /* Report items just scheduled for addition to revision control. */
  apr_status_t (*report_added_item) (const char *path, apr_pool_t *pool);

  /* Report items just scheduled for removal to revision control. */
  apr_status_t (*report_deleted_item) (const char *path, apr_pool_t *pool);

  /* Report items just reverted ('svn revert'). */
  apr_status_t (*report_reversion) (const char *path, apr_pool_t *pool);

  /* Report items just restored ('svn up'). */
  apr_status_t (*report_restoration) (const char *path, apr_pool_t *pool);
  
  /* Generic human-readable we-think-it's-non-fatal warning.  This
     function can parse STATUS and decide whether a "real" error
     should be returned. */
  apr_status_t (*report_warning) (apr_status_t status, const char *warning);

  /* Progress indication, yielding what PERCENTAGE (from 0-100) of a
     given ACTION has been completed. */
  apr_status_t (*report_progress) (const char *action,
                                   int percentage);

} svn_pool_feedback_t;

/* Retrieve a pointer to the global feedback vtable structure, which
   lives in top-level Subversion pools. */
svn_pool_feedback_t *svn_pool_get_feedback_vtable (apr_pool_t *p);


#ifndef SVN_POOL_DEBUG
/* Return a new pool.  If PARENT_POOL is non-null, then the new
 * pool will be a subpool of it, and will inherit the containing
 * pool's dedicated error subpool and feedback stream.
 *
 * If PARENT_POOL is NULL, then the returned pool will be a new
 * "global" pool (with no parent), and an error pool and feedback
 * stream will be created.
 *
 * If anything goes wrong with the pool creation, then an abort function
 * will be called, which will exit the program. If future allocations from
 * this pool cannot be fulfilled, then the abort function will be called,
 * terminating the program.  */
apr_pool_t *svn_pool_create (apr_pool_t *parent_pool);

#else /* SVN_POOL_DEBUG */
apr_pool_t *svn_pool_create_debug (apr_pool_t *parent_pool,
                                   const char *file,
                                   int line);
#define svn_pool_create(p) svn_pool_create_debug(p, __FILE__, __LINE__)
#endif /* SVN_POOL_DEBUG */



#ifndef SVN_POOL_DEBUG
/* Clear the passed in pool.
 *
 * The reason we need this wrapper to apr_pool_clear, is because
 * apr_pool_clear removes the association with the appropriate error
 * pool and feedback stream. This wrapper calls apr_pool_clear, and
 * then reattaches or recreates the error pool and feedback stream.
 *
 * If anything goes wrong, an abort function will be called.  */
void svn_pool_clear (apr_pool_t *p);

#else /* SVN_POOL_DEBUG */
void svn_pool_clear_debug (apr_pool_t *p,
                           const char *file,
                           int line);
#define svn_pool_clear(p) svn_pool_clear_debug(p, __FILE__, __LINE__)
#endif /* SVN_POOL_DEBUG */


#ifndef SVN_POOL_DEBUG

/* Destroy a POOL and all of its children. 
 *
 * This wrapper to apr_pool_destroy exists for symmatry (the
 * not-so-grand reason) and for the existence of a great memory usage
 * debugging hook (the grand reason).
 */
void svn_pool_destroy (apr_pool_t *p);

#else /* SVN_POOL_DEBUG */
void svn_pool_destroy_debug (apr_pool_t *p,
                             const char *file,
                             int line);
#define svn_pool_destroy(p) svn_pool_destroy_debug(p, __FILE__, __LINE__)
#endif /* SVN_POOL_DEBUG */


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_ERROR_H */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
