/* svn_error:  common exception handling for Subversion
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



#include "svn_error.h"


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

svn_error_t *
svn_create_error (ap_status_t err,
                  svn_boolean_t fatal, 
                  char *message,
                  svn_error_t *child;
                  ap_pool_t *pool)
{
  svn_error_t *new_error;     /* The error we're going to return */
  char *strerror_msg;         /* A place to store result of strerror() */

  /* Create the new error structure */
  *new_error = (svn_error_t *) ap_palloc (pool,
                                          sizeof(svn_error_t));

  /* Create space for strerror()'s result */
  char *strerror_msg = ap_palloc (pool, 100);

  new_error->err = err;
  new_error->fatal = fatal;
  new_error->message = message;
  new_error->child = child;

  new_error->canonical_errno = ap_canonical_error (errno);
  ap_strerror (err, strerror_msg, 100);
  new_error->description = strerror_msg;

  new_error->pool = pool;  

  return new_error;
}



/* A quick n' easy way to create a wrappered exception with your own
   message, before throwing it up the stack.  (It uses all of the
   child's fields by default.)  */

svn_error_t *
svn_quick_wrap_error (svn_error_t *child, char *new_msg)
{
  return (svn_create_error (child->err, child->fatal, new_msg,
                            child, child->pool));
}
                




/* Very dumb "default" error handler that anyone can use if they wish.

   Just prints out error stack (recursively), 
   and quits if the fatal flag is set.

*/

void
svn_handle_error (svn_error_t *err)
{

  /* Pretty-print the error */
  /* Note: we can also log errors here someday. */
  printf ("\nsvn_error: errno %d, %s\n", 
          err->err, err->description);
  printf ("      %s\n", err->message);
  fflush (stdout);

  if (err->child == NULL)  /* bottom of exception stack */
    {
      /* Bail if fatal */
      if (err->fatal)
        {
          printf ("Fatal error, exiting.\n");
          exit (err->err);
        }
      
      return;
    }

  /* Recurse */
  svn_handle_error (err->child);

  /* Bail if fatal */
  if (err->fatal)
    {
      printf ("Fatal error, exiting.\n");
      exit (err->err);
    }
}





/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
