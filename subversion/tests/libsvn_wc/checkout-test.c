/*
 * checkout-test.c :  testing checkout
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


static svn_error_t *
apply_delta (void *delta_src,
             svn_delta_read_fn_t *read_fn,
             svn_string_t *dest,
             svn_string_t *repos,
             svn_vernum_t version,
             apr_pool_t *pool)
{
  const svn_delta_walk_t *walker;
  void *walk_baton;
  void *dir_baton;
  svn_error_t *err;

  /* Get the change-walker and friends... */
  err = svn_wc_get_change_walker (dest, repos, version,
                                  &walker, &walk_baton, &dir_baton, pool);
  if (err)
    return err;

  /* ... and walk! */
  return svn_xml_parse (read_fn, delta_src,
                        walker, walk_baton, dir_baton, pool);
}


int
main (int argc, char **argv)
{
  apr_pool_t *pool = NULL;
  apr_status_t apr_err = 0;
  apr_file_t *src = NULL;     /* init to NULL very important! */
  svn_error_t *err = NULL;
  svn_string_t *target = NULL;  /* init to NULL also important here,
                                   because NULL implies delta's top dir */
  char *src_file = NULL;

  apr_initialize ();
  apr_create_pool (&pool, NULL);

  if ((argc < 2) || (argc > 3))
    {
      fprintf (stderr, "usage: %s DELTA_SRC_FILE [TARGET_NAME]\n", argv[0]);
      return 1;
    }
  else
    src_file = argv[1];

  apr_err = apr_open (&src, src_file,
                      (APR_READ | APR_CREATE),
                      APR_OS_DEFAULT,
                      pool);

  if (apr_err)
    {
      fprintf (stderr, "error opening %s: %s",
               src_file, apr_canonical_error (apr_err));
      exit (1);
    }

  if (argc == 3)
    target = svn_string_create (argv[2], pool);

  err = apply_delta
    (src, 
     test_read_fn,
     target,
     svn_string_create (":ssh:jrandom@svn.tigris.org/repos", pool),
     1,  /* kff todo: version must be passed in, right? */
     pool);
  
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
