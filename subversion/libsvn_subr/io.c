/*
 * io.c:   shared file reading, writing, and probing code.
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
 * individuals and marine fronds on behalf of CollabNet.
 */



#include <apr_pools.h>
#include <apr_file_io.h>
#include "svn_types.h"
#include "svn_path.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_io.h"

svn_error_t *
svn_io_check_path (svn_string_t *path,
                   enum svn_node_kind *kind,
                   apr_pool_t *pool)
{
  apr_dir_t *d = NULL;
  apr_file_t *f = NULL;
  apr_status_t apr_err;

  /* Try it as a dir first. */
  apr_err = apr_opendir (&d, path->data, pool);
  if (! apr_err)
    {
      *kind = svn_dir_kind;
      apr_err = apr_closedir (d);
      if (apr_err)
        return svn_error_createf (apr_err, 0, NULL, pool,
                                  "svn_io_check_path: "
                                  "problem closing dir %s",
                                  path->data);
      else
        return SVN_NO_ERROR;
    }
  else if (apr_err && (apr_err == APR_ENOENT))
    {
      *kind = svn_invalid_kind;
      return SVN_NO_ERROR;
    }
  else if (apr_err && (apr_err != APR_ENOTDIR))
    {
      return svn_error_createf (apr_err, 0, NULL, pool,
                                "svn_io_check_path: "
                                "opendir %s failed, but not with ENOTDIR",
                                path->data);
    }
  
  /* todo: handle symlink case, others, someday? */

  /* Else try it as a file. */

  apr_err = apr_open (&f, path->data, APR_READ, APR_OS_DEFAULT, pool);
  if (! apr_err)
    {
      *kind = svn_file_kind;
      apr_err = apr_close (f);
      if (apr_err)
        return svn_error_createf (apr_err, 0, NULL, pool,
                                  "svn_io_check_path: "
                                  "problem closing file %s",
                                  path->data);
      return SVN_NO_ERROR;
    }
  else if (apr_err && (apr_err == APR_ENOENT))
    {
      *kind = svn_invalid_kind;
      return SVN_NO_ERROR;
    }
  else
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "svn_io_check_path: "
                              "problem opening file %s",
                              path->data);
}




/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
