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




#ifndef SVN_ERROR_H
#define SVN_ERROR_H


#include <svn_types.h>
#include <svn_string.h>
#include <apr_errno.h>     /* APR's error system */
#include <stdio.h>


#define SVN_NO_ERROR   0   /* the best kind of (svn_error_t *) ! */

/* 
   Define custom Subversion error numbers, in the range reserved for
   that in APR: from APR_OS_START_USEERR to APR_OS_START_SYSERR (see
   apr_errno.h).
*/
#define SVN_WARNING                              (APR_OS_START_USEERR + 1)
#define SVN_ERR_NOT_AUTHORIZED                   (APR_OS_START_USEERR + 2)
#define SVN_ERR_PLUGIN_LOAD_FAILURE              (APR_OS_START_USEERR + 3)
#define SVN_ERR_UNKNOWN_FS_ACTION                (APR_OS_START_USEERR + 4)
#define SVN_ERR_UNEXPECTED_EOF                   (APR_OS_START_USEERR + 5)
#define SVN_ERR_MALFORMED_FILE                   (APR_OS_START_USEERR + 6)

/* The xml delta we got was not valid. */
#define SVN_ERR_MALFORMED_XML                    (APR_OS_START_USEERR + 7)

/* Can't do this update or checkout, because something was in the way. */
#define SVN_ERR_OBSTRUCTED_UPDATE                (APR_OS_START_USEERR + 8)



typedef struct svn_error
{
  apr_status_t apr_err;       /* APR error value, possibly SVN_ custom err */
  int src_err;               /* native error code (e.g. errno, h_errno...) */
  const char *message;       /* details from producer of error */
  struct svn_error *child;   /* ptr to the error we "wrap" */
  apr_pool_t *pool;           /* place to generate message strings from */

} svn_error_t;




/*
  svn_create_error() : for creating nested exception structures.

  Input:  an APR or SVN custom error code,
          the original errno,
          a descriptive message,
          a "child" error to wrap,
          a pool for alloc'ing

  Returns:  a new error structure (containing the old one).

     ** If creating the "bottommost" error in a chain,
        pass NULL as the fourth (child) argument.

 */

svn_error_t *svn_create_error (apr_status_t apr_err,
                               int src_err,
                               const char *message,
                               svn_error_t *child,
                               apr_pool_t *pool);



/* A quick n' easy way to create a wrappered exception with your own
   message, before throwing it up the stack.  (It uses all of the
   child's fields.)  */

svn_error_t * svn_quick_wrap_error (svn_error_t *child, const char *new_msg);


/* Very dumb "default" error handler that anyone can use if they wish. */

void svn_handle_error (svn_error_t *error, FILE *stream);

/* Very dumb "default" warning handler -- used by all policies, unless
   svn_svr_warning_callback() is used to set the warning handler
   differently.  */

void svn_handle_warning (void *data, char *fmt, ...);


#endif   /* SVN_ERROR_H */


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
