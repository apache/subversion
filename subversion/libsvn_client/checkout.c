/*
 * checkout.c:  wrappers around wc checkout functionality
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
#include "svn_wc.h"
#include "svn_delta.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"



/*** Helpers ***/

static svn_error_t *
generic_read (void *baton, char *buffer, apr_size_t *len, apr_pool_t *pool)
{
  apr_file_t *src = (apr_file_t *) baton;
  apr_status_t stat;
  
  stat = apr_full_read (src, buffer, (apr_size_t) *len, (apr_size_t *) len);
  
  if (stat && (stat != APR_EOF))
    return
      svn_error_create (stat, 0, NULL, pool,
                        "error reading incoming delta stream");
  
  else 
    return 0;  
}


static svn_error_t *
apply_delta (void *delta_src,
             svn_read_fn_t *read_fn,
             svn_string_t *dest,
             svn_string_t *repos,
             svn_string_t *ancestor_path,
             svn_vernum_t ancestor_version,
             apr_pool_t *pool)
{
  const svn_delta_edit_fns_t *editor;
  void *edit_baton;
  svn_error_t *err;

  if (! ancestor_path)
    ancestor_path = svn_string_create ("", pool);
  if (ancestor_version == SVN_INVALID_VERNUM)
    ancestor_version = 1;

  /* Get the editor and friends... */
  err = svn_wc_get_checkout_editor
    (dest,
     repos,
     ancestor_path,
     ancestor_version,
     &editor,
     &edit_baton,
     pool);
  if (err)
    return err;

  /* ... and edit! */
  return svn_delta_xml_auto_parse (read_fn,
                                   delta_src,
                                   editor,
                                   edit_baton,
                                   ancestor_path,
                                   ancestor_version,
                                   pool);
}



/*** Public Interfaces. ***/

svn_error_t *
svn_client_checkout (svn_string_t *path,
                     svn_string_t *xml_src,
                     svn_string_t *ancestor_path,
                     svn_vernum_t ancestor_version,
                     apr_pool_t *pool)
{
  svn_error_t *err;
  apr_status_t apr_err;
  char *repos = ":ssh:jrandom@subversion.tigris.org/repos";
  apr_file_t *in = NULL;

  assert (path != NULL);
  assert (xml_src != NULL);

  /* Open the XML source file. */
  apr_err = apr_open (&in, xml_src->data,
                      (APR_READ | APR_CREATE),
                      APR_OS_DEFAULT,
                      pool);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "unable to open %s", xml_src->data);

  /* Check out the delta. */
  err = apply_delta (in,
                     generic_read,
                     path,
                     svn_string_create (repos, pool),
                     ancestor_path,
                     ancestor_version,
                     pool);
  if (err)
    {
      apr_close (in);
      return err;
    }

  apr_close (in);

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
