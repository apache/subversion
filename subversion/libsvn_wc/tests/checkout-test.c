/*
 * checkout-test.c :  testing checkout
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
 * This software consists of voluntary contributions made by many
 * individuals on behalf of Collab.Net.
 */



#include <stdio.h>
#include <stdlib.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_file_io.h>
#include "svn_types.h"
#include "svn_delta.h"
#include "svn_wc.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_hash.h"



static svn_error_t *
test_read_fn (void *baton, char *buffer, apr_off_t *len, apr_pool_t *pool)
{
  apr_file_t *src = (apr_file_t *) baton;
  svn_error_t *err;
  apr_status_t stat;

  stat = apr_full_read (src, buffer, (apr_size_t) *len, (apr_size_t *) len);

  if (stat && (stat != APR_EOF))
    return
      svn_create_error (stat, 0, "error reading incoming delta stream",
                        NULL, pool);
  
  else 
    return 0;  
}


int
main (int argc, char **argv)
{
  apr_pool_t *pool = NULL;
  apr_status_t apr_err = 0;
  apr_file_t *src = NULL;     /* init to NULL very important! */
  svn_error_t *err = NULL;
  svn_string_t *target = NULL;

  if (argc < 2)
    {
      fprintf (stderr, "need TARGET argument\n");
      return 1;
    }

  apr_initialize ();
  apr_create_pool (&pool, NULL);

  /* On Tue, Aug 08, 2000 at 09:50:33PM -0500, Karl Fogel wrote:
   * > Greg, I've got a stupid question for you:
   * > 
   * > How do I get an (apr_file_t *) that reads from stdin?  And one writing
   * > to stdout?
   * 
   * Check out apr_put_os_file() in apr_portable.h
   */

  apr_err = apr_open (&src, "checkout-1.delta",
                      (APR_READ | APR_CREATE),
                      APR_OS_DEFAULT,
                      pool);

  if (apr_err)
    {
      fprintf (stderr, "error opening checkout-1.delta: %s",
               apr_canonical_error (apr_err));
      exit (1);
    }

  target = svn_string_create (argv[1], pool);

  err = svn_wc_apply_delta (src, test_read_fn, target, pool);

  if (err)
    svn_handle_error (err, stdout);

  apr_close (src);

  return 0;
}





/* -----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */
