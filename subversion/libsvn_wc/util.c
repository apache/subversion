/*
 * util.c:  general routines defying categorization; eventually I
 *          suspect they'll end up in libsvn_subr, but don't want to
 *          pollute that right now.  Note that nothing in here is
 *          specific to working copies.
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
 * This software consists of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 */



#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_time.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "wc.h"   /* just for prototypes of things in this .c file */


svn_error_t *
svn_wc__ensure_directory (svn_string_t *path, apr_pool_t *pool)
{
  svn_string_t *npath = svn_string_dup (path, pool);
  apr_status_t apr_err = 0;
  apr_dir_t *this_attempt_dir = NULL;

  apr_err = apr_opendir (&this_attempt_dir, npath->data, pool);

  if (apr_err && (apr_err != APR_ENOENT))
    {
      /* If got an error other than dir non-existence, then we can't
         ensure this directory's existence, so just return the error.
         Might happen if there's a file in the way, for example. */
      return svn_create_error (apr_err, 0, npath->data, NULL, pool);
    }
  else if (apr_err == APR_ENOENT)  /* (yes, redundant conditional) */
    {
      /* The dir doesn't exist, and it's our job to change that. */

      apr_err = apr_make_dir (npath->data, APR_OS_DEFAULT, pool);

      if (apr_err && (apr_err != APR_ENOENT))
        {
          /* Tried to create the dir, and encountered some problem
             other than non-existence of intermediate dirs.  We can't
             ensure the desired directory's existence, so just return
             the error. */ 
          return svn_create_error (apr_err, 0, npath->data, NULL, pool);
        }
      else if (apr_err == APR_ENOENT) /* (redundant conditional and comment) */
        {
          /* Okay, so the problem is a missing intermediate
             directory.  We don't know which one, so we recursively
             back up and try again. */
          svn_string_t *shorter_path
            = svn_path_remove_component (npath, SVN_PATH_LOCAL_STYLE, pool);

          if (svn_string_isempty (shorter_path))
            {
              /* A weird and probably rare situation. */
              return svn_create_error (0, 0,
                                       "unable to make any directories",
                                       NULL, pool);
            }
          else  /* We have a valid path, so recursively ensure it. */
            {
              svn_error_t *err = svn_wc__ensure_directory (shorter_path, pool);
          
              if (err)
                return (err);
              else
                return svn_wc__ensure_directory (npath, pool);
            }
        }
    }
  else  /* No problem, the dir already existed, so just close it and leave. */
    apr_err = apr_closedir (this_attempt_dir);
  
  if (apr_err)
    return svn_create_error (apr_err, 0, npath->data, NULL, pool);
  else
    return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
