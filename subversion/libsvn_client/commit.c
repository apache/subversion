/*
 * commit.c:  wrappers around wc commit functionality.
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

/* ==================================================================== */



/*** Includes. ***/

#include <assert.h>
#include <apr_strings.h>
#include "svn_wc.h"
#include "svn_delta.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_test.h"
#include "svn_io.h"




/*** Public Interface. ***/

svn_error_t *
svn_client_commit (svn_string_t *path,
                   svn_string_t *xml_dst,
                   svn_vernum_t version,  /* this param is temporary */
                   apr_pool_t *pool)
{
  svn_error_t *err;
  apr_status_t apr_err;
  apr_file_t *dst = NULL; /* old habits die hard */
  const svn_delta_edit_fns_t *editor;
  void *edit_baton;
  apr_hash_t *targets = NULL;

  /* Step 1: look for local mods and send 'em out. */
  apr_err = apr_open (&dst, xml_dst->data,
                      (APR_WRITE | APR_CREATE),
                      APR_OS_DEFAULT,
                      pool);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "error opening %s", xml_dst->data);

  err = svn_delta_get_xml_editor (svn_io_file_writer,
                                  dst,
                                  &editor,
                                  &edit_baton,
                                  pool);
  if (err)
    return err;

  if (! path)
    path = svn_string_create (".", pool);
  err = svn_wc_crawl_local_mods (&targets,
                                 path,
                                 editor,
                                 edit_baton,
                                 pool);
  if (err)
    return err;

  apr_err = apr_close (dst);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "error closing %s", xml_dst->data);
  
  /* Step 2: tell the working copy the commit succeeded. */
  err = svn_wc_close_commit (path, version, targets, pool);
  if (err)
    return err;

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
