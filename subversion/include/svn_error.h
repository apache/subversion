/* svn_error.h:  common exception handling for Subversion
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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




#ifndef SVN_ERROR_H
#define SVN_ERROR_H

#include <apr.h>
#include <apr_errno.h>     /* APR's error system */
#include <apr_pools.h>

#define APR_WANT_STDIO
#include <apr_want.h>

#include <svn_types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define SVN_NO_ERROR   0   /* the best kind of (svn_error_t *) ! */

/* The actual error codes are kept in a separate file; see comments
   there for the reasons why. */
#include "svn_error_codes.h"

/* Set the error location for debug mode. */
void svn_error__locate (const char *file, long line);


/* Put a English description of STATCODE into BUF and return BUF,
   null-terminated.  STATCODE is either an svn error or apr error.  */
char *svn_strerror (apr_status_t statcode, char *buf, apr_size_t bufsize);



/*** SVN error creation and destruction. ***/

/*
  svn_error_create() : for creating nested exception structures.

  Input:  an APR or SVN custom error code,
          the original errno,
          a "child" error to wrap,
          a descriptive message,

  Returns:  a new error structure (containing the old one).

  Notes: Errors are always allocated in a subpool of the global pool,
         since an error's lifetime is generally not related to the
         lifetime of the any convenient pool.  Errors must be be freed
         with svn_error_clear().

         If creating the "bottommost" error in a chain, pass NULL for
         the child argument.
 */
svn_error_t *svn_error_create (apr_status_t apr_err,
                               int src_err,
                               svn_error_t *child,
                               const char *message);

/* Wrapper macro to collect file and line information */
#define svn_error_create \
  (svn_error__locate(__FILE__,__LINE__), (svn_error_create))

/* Create an error structure with the given APR_ERR, SRC_ERR, and
   CHILD, with a printf-style error message produced by passing FMT,
   ... through apr_psprintf.  */
svn_error_t *svn_error_createf (apr_status_t apr_err,
                                int src_err,
                                svn_error_t *child,
                                const char *fmt, 
                                ...)
       __attribute__ ((format (printf, 4, 5)));

/* Wrapper macro to collect file and line information */
#define svn_error_createf \
  (svn_error__locate(__FILE__,__LINE__), (svn_error_createf))

/* A quick n' easy way to create a wrappered exception with your own
   message, before throwing it up the stack.  (It uses all of the
   child's fields.)  */
svn_error_t *svn_error_quick_wrap (svn_error_t *child, const char *new_msg);

/* Wrapper macro to collect file and line information */
#define svn_error_quick_wrap \
  (svn_error__locate(__FILE__,__LINE__), (svn_error_quick_wrap))

/* Add NEW_ERR to the end of CHAIN's chain of errors.  The NEW_ERR
 * chain will be copied into CHAIN's pool and destroyed, so NEW_ERR
 * itself becomes invalid after this function. */
void svn_error_compose (svn_error_t *chain, svn_error_t *new_err);


/* Free the memory used by ERROR, as well as all ancestors and
   descendents of ERROR.  Unlike other Subversion objects, errors are
   managed explicitly; you MUST clear an error if you are ignoring it,
   or you are leaking memory.  For convenience, ERROR may be NULL, in
   which case this function does nothing; thus,
   svn_error_clear(svn_foo(...)) works as an idiom to ignore errors. */
void svn_error_clear (svn_error_t *error);


/* Very basic default error handler: print out error stack, and quit
   iff the FATAL flag is set. */
void svn_handle_error (svn_error_t *error,
                       FILE *stream,
                       svn_boolean_t fatal);

/* Basic, default warning handler. Just prints to a FILE* stream,
   which must be passed in DATA.  Allocates from a subpool of POOL. */
void svn_handle_warning (apr_pool_t *pool, void *data, const char *fmt, ...);


/* A statement macro for checking error return values.
   Evaluate EXPR.  If it yields an error, return that error from the
   current function.  Otherwise, continue.

   The `do { ... } while (0)' wrapper has no semantic effect, but it
   makes this macro syntactically equivalent to the expression
   statement it resembles.  Without it, statements like

     if (a)
       SVN_ERR (some operation);
     else
       foo;

   would not mean what they appear to.  */

#define SVN_ERR(expr)                           \
  do {                                          \
    svn_error_t *svn_err__temp = (expr);        \
    if (svn_err__temp)                          \
      return svn_err__temp;                     \
  } while (0)


/* A statement macro, very similar to SVN_ERR. This macro will wrap the
   error with the specified text before returning the error. */

#define SVN_ERR_W(expr, wrap_msg)                           \
  do {                                                      \
    svn_error_t *svn_err__temp = (expr);                    \
    if (svn_err__temp)                                      \
      return svn_error_quick_wrap(svn_err__temp, wrap_msg); \
  } while (0)


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_ERROR_H */
