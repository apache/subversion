/* svn_error.h:  common exception handling for Subversion
 *
 * ================================================================
 * Copyright (c) 2000 Collab.Net.  All rights reserved.
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
 * software developed by Collab.Net (http://www.Collab.Net/)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of Collab.Net.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLAB.NET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
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
 * individuals on behalf of Collab.Net.
 */




#ifndef __SVN_ERROR_H__
#define __SVN_ERROR_H__


#include <svn_types.h>
#include <svn_string.h>
#include <apr_errno.h>     /* APR's error system */
#include <stdio.h>



/* REALLY USEFUL macros for throwing errors quickly and easily! */
#define SVN_RETURN_IF_ERROR(err) \
if (err) return (err)

#define SVN_RETURN_WRAPPED_ERROR(err, msg) \
if (err) return (svn_quick_wrap_error((err),(msg)))


#define SVN_SUCCESS   0

#define SVN_FATAL     1    /* Use instead of TRUE or 1, for readability. */
#define SVN_NON_FATAL 0    /* Use instead of FALSE or 0, for readability. */


/* 
   Theoretically, this is the header file where we can define our own
   *custom* Subversion errno's, specifically between the ranges of
   APR_OS_START_USEERR and APR_OS_START_SYSERR (see apr_errno.h)
*/

#define SVN_ERR_NOT_AUTHORIZED                   (APR_OS_START_USEERR + 1)
#define SVN_ERR_UNRECOGNIZED_SECTION             (APR_OS_START_USEERR + 2)
#define SVN_ERR_MALFORMED_LINE                   (APR_OS_START_USEERR + 3)


typedef struct svn_error_t
{
  ap_status_t err;             /* native OS errno */
  svn_boolean_t fatal;         /* does the creator think this a fatal error? */
  char *message;               /* details from producer of error */
  struct svn_error_t *child;   /* ptr to next error below this one */

  int canonical_errno;         /* "canonicalized" errno from APR */ 
  char *description;           /* generic description from ap_strerror() */

  ap_pool_t *pool;             /* place to generate message strings from */

} svn_error_t;



/* svn_error_t constructor */

#define SVN_FATAL     1    /* Use instead of TRUE or 1, for readability. */
#define SVN_NON_FATAL 0    /* Use instead of FALSE or 0, for readability. */


/*
  svn_create_error() : for creating nested exception structures.

  Input:  an error code,
          is-error-fatal-p?,
          a descriptive message,
          a "child" exception,
          a pool for alloc'ing

  Returns:  a new error structure (containing the old one).


  Usage: 

          1.  If this is a BOTTOM level error (i.e. the first one
          thrown), you MUST set child to NULL and pass a real pool_t.
          
             my_err = svn_create_error (errno, SVN_NON_FATAL,
                                        "Can't find repository",
                                        NULL, my_pool);

          2.  If this error WRAPS a previous error, include a non-NULL
          child to wrap.  You can use the child's pool if you wish.

             next_err = svn_create_error (errno, SVN_NON_FATAL,
                                          "Filesystem access failed",
                                          previous_err, previous_err->pool);

 */

svn_error_t *svn_create_error (ap_status_t err,
                               svn_boolean_t fatal,
                               svn_string_t *message,
                               svn_error_t *child,
                               ap_pool_t *pool);



/* A quick n' easy way to create a wrappered exception with your own
   message, before throwing it up the stack.  (It uses all of the
   child's fields.)  */

svn_error_t * svn_quick_wrap_error (svn_error_t *child, char *new_msg)


/* Very dumb "default" error handler that anyone can use if they wish. */

void svn_handle_error (svn_error_t *error);




#endif   /* __SVN_ERROR_H__ */


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
